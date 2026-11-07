// Off-device simulator for the stack generator. It runs the generated trigger plans with a small
// model of Geometry Dash's Pickup / Count / Toggle / Touch semantics and checks them against
// plain reference models.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -o sim_test tests/sim_test.cpp minimal/src/stacks/Plan.cpp && ./sim_test
//
// Model: groups are on by default; an object is active while its group is on. A Touch /
// Collision trigger spawns its target group on a click. Spawning a group fires every active
// spawn-triggered Pickup/Toggle in it. Whenever an item changes, every active Count trigger
// watching that item whose value now equals its target spawns its target group (the watchers
// are collected at the moment of the change, so groups toggled while handling it do not
// re-fire for that same change). Toggles with spawn=false run once at the start.
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <vector>

#include "../minimal/src/stacks/Plan.hpp"

using namespace tpll;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failed;                                                               \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
        }                                                                             \
    } while (0)

struct Sim {
    Plan const& p;
    std::unordered_map<int, bool> off;                 // group -> switched off
    std::unordered_map<int, int64_t> item;
    std::unordered_map<int, std::vector<int>> byGroup;  // group -> object indices
    std::unordered_map<int, std::vector<int>> watchers; // item -> Count object indices
    std::vector<int> sources;
    long long spawns = 0;

    explicit Sim(Plan const& plan) : p(plan) {
        for (int i = 0; i < (int)p.objs.size(); ++i) {
            auto const& o = p.objs[i];
            if (o.group > 0) byGroup[o.group].push_back(i);
            if (o.kind == Kind::Count) watchers[o.item].push_back(i);
            if (o.kind == Kind::Touch || o.kind == Kind::CollisionTrigger) sources.push_back(i);
        }
        for (auto const& o : p.objs)
            if (o.kind == Kind::Toggle && !o.spawn) off[o.target] = !o.on;
    }
    bool groupOn(int g) const {
        auto it = off.find(g);
        return it == off.end() || !it->second;
    }
    bool active(Obj const& o) const { return o.group <= 0 || groupOn(o.group); }

    void spawn(int g, int depth = 0) {
        assert(depth < 200);
        ++spawns;
        auto it = byGroup.find(g);
        if (it == byGroup.end()) return;
        auto idx = it->second;  // copy: handling may toggle groups
        for (int i : idx) {
            auto const& o = p.objs[i];
            if (!o.spawn || !active(o)) continue;
            if (o.kind == Kind::Pickup) setItem(o.item, o.override_ ? o.count : item[o.item] + o.count, depth);
            else if (o.kind == Kind::Toggle) off[o.target] = !o.on;
        }
    }
    void setItem(int id, int64_t v, int depth) {
        item[id] = v;
        auto w = watchers.find(id);
        if (w == watchers.end()) return;
        std::vector<int> fire;
        for (int i : w->second) {
            auto const& o = p.objs[i];
            if (active(o) && v == o.count) fire.push_back(i);
        }
        for (int i : fire) spawn(p.objs[i].target, depth + 1);
    }
    // A click activates every click source that is active at that moment (decided before any
    // of them runs), so a stack that finishes on this click does not also take the same click.
    void click() {
        std::vector<int> fire;
        for (int i : sources)
            if (active(p.objs[i])) fire.push_back(i);
        for (int i : fire) spawn(p.objs[i].target);
    }
    int64_t get(int id) { return item.count(id) ? item[id] : 0; }
};

static Config base(int stacks, int64_t clicks) {
    Config c;
    c.stacks = stacks;
    c.clicks = clicks;
    return c;
}

static void expectConnected(Plan const& p, char const* what) {
    auto probs = validate(p);
    if (!probs.empty()) std::printf("  [%s] validate: %s\n", what, probs[0].c_str());
    CHECK(p.error.empty());
    CHECK(probs.empty());
}

// ---- Normal mode: stack cycling ----------------------------------------------------------------
static void testNormal() {
    for (int collision = 0; collision < 2; ++collision)
        for (int n = 1; n <= 5; ++n)
            for (int64_t clicks : {1, 2, 5, 7}) {
                Config c = base(n, clicks);
                c.collision = collision;
                c.itemStart = 10;
                c.groupStart = 20;
                c.blockStart = 3;
                Plan p = build(c, 300.f, 300.f);
                expectConnected(p, "normal");
                Sim s(p);
                // reference: stack `cur` takes clicks, resets at `clicks`, next stack takes over
                std::vector<int64_t> ref(n, 0);
                int cur = 0;
                for (int k = 0; k < 4 * n * (int)clicks + 3; ++k) {
                    s.click();
                    ref[cur]++;
                    if (ref[cur] == clicks) { ref[cur] = 0; cur = (cur + 1) % n; }
                    for (int i = 0; i < n; ++i) {
                        if (s.get(c.itemStart + i) != ref[i]) {
                            CHECK(false);
                            std::printf("  normal n=%d clicks=%lld click %d stack %d: got %lld want %lld\n", n,
                                        (long long)clicks, k, i, (long long)s.get(c.itemStart + i), (long long)ref[i]);
                            return;
                        }
                    }
                }
                CHECK(true);
            }
    // 999,999 clicks on one stack: needs exactly that many clicks, then wraps to 0
    {
        Config c = base(1, kNormalCap);
        Plan p = build(c, 0, 0);
        Sim s(p);
        for (int k = 0; k < 999'998; ++k) s.click();
        CHECK(s.get(1) == 999'998);
        s.click();
        CHECK(s.get(1) == 0);
    }
}

// ---- Omega: the 32-bit limit ------------------------------------------------------------------
static void testOmega() {
    Config c = base(2, kOmegaCap);
    c.omega = true;
    Plan p = build(c, 0, 0);
    expectConnected(p, "omega");
    CHECK(p.cfg.clicks == INT_MAX);
    Sim s(p);
    s.item[1] = INT_MAX - 2;
    s.click();
    CHECK(s.get(1) == INT_MAX - 1);
    s.click();  // reaches INT_MAX: the stack is full, resets to 0 and hands over
    CHECK(s.get(1) == 0);
    s.click();
    CHECK(s.get(2) == 1);
    // omega is refused when not enabled: clicks clamp to 999,999
    Config d = base(1, 5'000'000);
    CHECK(build(d, 0, 0).cfg.clicks == kNormalCap);
}

// ---- Counter Mode against a reference model ---------------------------------------------------
static void refCounterClick(std::vector<int64_t>& c, int64_t cap, bool& stopped, int64_t perClick) {
    if (stopped) return;
    int n = (int)c.size() - 1;  // c[1..n]
    for (int q = 0; q < perClick; ++q) {
        c[1]++;
        if (c[1] == cap) {
            int j = -1;
            for (int s = 2; s <= n; ++s)
                if (c[s] < cap) { j = s; break; }
            if (j < 0) { stopped = true; }
            else { c[1] = 0; c[j]++; }
        }
        if (stopped) break;  // click group switched off: later +1s in this click do not run
    }
}

static void testCounters() {
    for (int collision = 0; collision < 2; ++collision)
        for (int n = 1; n <= 4; ++n)
            for (int64_t cap : {2, 3, 4, 5})
                for (int64_t per : {1, 2}) {
                    if (per >= cap) continue;
                    Config c = base(n, cap);
                    c.carry = true;
                    c.perClick = per;
                    c.collision = collision;
                    c.itemStart = 5;
                    c.groupStart = 40;
                    Plan p = build(c, 0, 0);
                    expectConnected(p, "counters");
                    if (!p.error.empty()) continue;
                    Sim s(p);
                    std::vector<int64_t> ref(n + 1, 0);
                    bool stopped = false;
                    long long clicks = 0;
                    long long limit = 2 * (long long)cap * cap * (n + 1) + 20;
                    bool ok = true;
                    for (long long k = 0; k < limit; ++k) {
                        s.click();
                        refCounterClick(ref, cap, stopped, per);
                        ++clicks;
                        for (int i = 1; i <= n; ++i)
                            if (s.get(c.itemStart + i - 1) != ref[i]) {
                                ok = false;
                                std::printf("  counters n=%d cap=%lld per=%lld click %lld counter %d: got %lld want %lld\n",
                                            n, (long long)cap, (long long)per, k, i,
                                            (long long)s.get(c.itemStart + i - 1), (long long)ref[i]);
                                break;
                            }
                        if (!ok) break;
                    }
                    CHECK(ok);
                    // all counters finish full and the click group is off
                    for (int i = 1; i <= n; ++i) CHECK(s.get(c.itemStart + i - 1) == cap);
                    CHECK(!s.groupOn(c.groupStart));  // CLICK
                    CHECK(stopped);
                }
    // errors
    Config bad = base(2, 1);
    bad.carry = true;
    CHECK(!build(bad, 0, 0).error.empty());
    Config bad2 = base(2, 5);
    bad2.carry = true;
    bad2.perClick = 5;
    CHECK(!build(bad2, 0, 0).error.empty());
    Config bad3 = base(2, 500);
    bad3.carry = true;
    bad3.perClick = 101;
    CHECK(!build(bad3, 0, 0).error.empty());
}

// ---- Extended Integer Mode ----------------------------------------------------------------------
static void testExtended() {
    // small base: compare every click with the exact number, then saturation
    for (int collision = 0; collision < 2; ++collision)
        for (int limbs = 1; limbs <= 4; ++limbs)
            for (int64_t base10 : {2, 3, 10}) {
                for (int64_t per : {1, 3}) {
                    Config c = base(limbs, base10);
                    c.extended = true;
                    c.perClick = per;
                    c.collision = collision;
                    c.itemStart = 100;
                    c.groupStart = 200;
                    Plan p = build(c, 0, 0);
                    expectConnected(p, "extended");
                    if (!p.error.empty()) continue;
                    per = p.cfg.perClick;  // Add Per Click is clamped below the base
                    int64_t maxv = 1;
                    for (int i = 0; i < limbs; ++i) maxv *= base10;  // B^k
                    Sim s(p);
                    int64_t n = 0;
                    bool ok = true;
                    for (int64_t k = 0; k < maxv / per + 30; ++k) {
                        s.click();
                        n += per;
                        int64_t shown = n >= maxv ? maxv - 1 : n;  // saturates at B^k - 1
                        int64_t rest = shown;
                        for (int i = 0; i < limbs; ++i) {
                            int64_t want = rest % base10;
                            rest /= base10;
                            if (s.get(c.itemStart + i) != want) {
                                ok = false;
                                std::printf("  ext limbs=%d B=%lld per=%lld click %lld limb %d: got %lld want %lld\n", limbs,
                                            (long long)base10, (long long)per, (long long)k, i,
                                            (long long)s.get(c.itemStart + i), (long long)want);
                                break;
                            }
                        }
                        if (!ok) break;
                    }
                    CHECK(ok);
                    CHECK(!s.groupOn(c.groupStart));  // click source switched off after saturating
                    CHECK(!s.groupOn(c.groupStart + 3));  // block gone
                }
            }
    // base 1,000,000,000: carries across several limbs, no wrap to zero
    {
        Config c = base(4, kDefaultLimbBase);
        c.extended = true;
        c.itemStart = 1;
        c.groupStart = 1;
        Plan p = build(c, 0, 0);
        expectConnected(p, "extended-1e9");
        const int64_t B = kDefaultLimbBase;
        Sim s(p);
        s.item[1] = B - 1;
        s.click();
        CHECK(s.get(1) == 0 && s.get(2) == 1 && s.get(3) == 0 && s.get(4) == 0);
        // 999,999,999,999,999,999 + 1 = 1,000,000,000,000,000,000 (limb3 = 1)
        s.item[1] = B - 1; s.item[2] = B - 1; s.item[3] = 0;
        s.click();
        CHECK(s.get(1) == 0 && s.get(2) == 0 && s.get(3) == 1);
        // everything one below the top: the top limb takes the carry
        s.item[1] = B - 1; s.item[2] = B - 1; s.item[3] = B - 1; s.item[4] = 5;
        s.click();
        CHECK(s.get(1) == 0 && s.get(2) == 0 && s.get(3) == 0 && s.get(4) == 6);
        // the largest value: one more click saturates instead of wrapping to zero
        s.item[1] = B - 1; s.item[2] = B - 1; s.item[3] = B - 1; s.item[4] = B - 1;
        s.click();
        for (int i = 1; i <= 4; ++i) CHECK(s.get(i) == B - 1);
        CHECK(!s.groupOn(1));
        s.click();  // more clicks do nothing
        for (int i = 1; i <= 4; ++i) CHECK(s.get(i) == B - 1);
    }
    // base INT_MAX: still works with the biggest value an item can hold
    {
        Config c = base(3, kOmegaCap);
        c.extended = true;
        Plan p = build(c, 0, 0);
        Sim s(p);
        s.item[1] = INT_MAX - 1;
        s.click();
        CHECK(s.get(1) == 0 && s.get(2) == 1);
    }
    // limbs clamp, base clamp
    Config c = base(500, 1);
    c.extended = true;
    auto p = build(c, 0, 0);
    CHECK(p.cfg.stacks == kMaxLimbs && p.cfg.clicks == 2);
}

// ---- ID allocation ------------------------------------------------------------------------------
static void testIds() {
    Config c = base(3, 10);
    UsedIds used;
    CHECK(!findConflicts(c, used).any());
    used.groups = {3, 4};
    used.items = {2};
    auto cf = findConflicts(c, used);
    CHECK(cf.groups.size() == 2 && cf.items.size() == 1 && cf.blocks.empty());
    CHECK(autoFixIds(c, used));
    CHECK(!findConflicts(c, used).any());
    CHECK(c.groupStart == 5 && c.itemStart == 3);  // first free ranges
    // collision blocks only matter when collision mode is on
    UsedIds ub; ub.blocks = {1, 2, 3};
    Config c2 = base(3, 10);
    CHECK(!findConflicts(c2, ub).any());
    c2.collision = true;
    CHECK(findConflicts(c2, ub).any());
    CHECK(autoFixIds(c2, ub) && c2.blockStart == 4);
    // range that would run over 9999 wraps around to a free range below
    Config c3 = base(2, 10);
    c3.groupStart = 9999;
    UsedIds none;
    CHECK(autoFixIds(c3, none));
    CHECK(c3.groupStart + needsFor(c3).groups - 1 <= kMaxID);
    // nothing free at all
    UsedIds full;
    for (int i = 1; i <= kMaxID; i += 5) full.groups.insert(i);  // no 8-long free range
    Config c4 = base(2, 10);
    CHECK(!autoFixIds(c4, full));
    // build refuses ranges above 9999
    Config c5 = base(10, 10);
    c5.groupStart = 9990;
    CHECK(!build(c5, 0, 0).error.empty());
    // needs match what the generators really use
    for (int mode = 0; mode < 3; ++mode)
        for (int n = 1; n <= 6; ++n) {
            Config d = base(n, 7);
            d.carry = mode == 1;
            d.extended = mode == 2;
            d.collision = true;
            Plan p = build(d, 0, 0);
            auto nd = needsFor(p.cfg);
            CHECK(p.groupLast - p.groupFirst + 1 == nd.groups);
            CHECK(p.itemLast - p.itemFirst + 1 == nd.items);
            std::set<int> g;
            for (auto const& o : p.objs) {
                if (o.group > 0) g.insert(o.group);
                if (o.kind == Kind::Toggle || o.kind == Kind::Count || o.kind == Kind::Touch || o.kind == Kind::CollisionTrigger)
                    if (o.target > 0) g.insert(o.target);
            }
            for (int id : g) CHECK(id >= p.groupFirst && id <= p.groupLast);  // nothing outside the claimed range
        }
}

// ---- Validation catches broken plans -------------------------------------------------------------
static void testValidate() {
    Config c = base(3, 5);
    Plan p = build(c, 0, 0);
    CHECK(validate(p).empty());
    // remove the owner of a group: a Toggle now targets nothing
    Plan q = p;
    for (auto& o : q.objs)
        if (o.kind == Kind::Block) o.group = 0;
    CHECK(!validate(q).empty());
    // a pickup nobody spawns
    Plan r = p;
    for (auto& o : r.objs)
        if (o.kind == Kind::Touch) o.target = 0;
    CHECK(!validate(r).empty());
    // a Count watching an item nothing writes
    Plan t = p;
    for (auto& o : t.objs)
        if (o.kind == Kind::Pickup) o.item += 1000;
    CHECK(!validate(t).empty());
}

// ---- Level string + saved config -----------------------------------------------------------------
static void testSerialize() {
    Config c = base(2, 5);
    Plan p = build(c, 315.f, 375.f);
    auto s = toLevelString(p);
    size_t semi = 0;
    for (char ch : s) semi += ch == ';';
    CHECK(semi == p.objs.size());
    CHECK(s.find("1,1817,2,345,3,375,80,1,77,1,57,2,62,1,87,1;") != std::string::npos);   // +1 pickup of stack 1
    CHECK(s.find("1,1611,2,375,3,375,80,1,77,5,51,3,56,1,104,1;") != std::string::npos);  // Count trigger (target END)
    CHECK(s.find("1,1049,2,435,3,375,51,4,56,0,57,3,62,1,87,1;") != std::string::npos);   // Toggle block off
    CHECK(s.find("1,1595,2,315,3,375,51,2,57,1;") != std::string::npos);                  // Touch trigger
    CHECK(s.find("1,1615,") != std::string::npos);
    Config d = base(2, 5);
    d.collision = true;
    auto s2 = toLevelString(build(d, 0, 0));
    CHECK(s2.find("1,1815,") != std::string::npos && s2.find("1,1816,") != std::string::npos);
    // config round trip
    Config e;
    e.stacks = 7; e.clicks = 123456; e.omega = true; e.itemStart = 11; e.groupStart = 22; e.carry = true;
    e.perClick = 4; e.collision = true; e.blockStart = 9; e.extended = true; e.autoFix = false;
    auto f = decodeConfig(encodeConfig(e));
    CHECK(f.stacks == 7 && f.clicks == 123456 && f.omega && f.itemStart == 11 && f.groupStart == 22 && f.carry);
    CHECK(f.perClick == 4 && f.collision && f.blockStart == 9 && f.extended && !f.autoFix);
    // old 9-field saves still load
    auto g = decodeConfig("3,999999,0,1,1,0,1,0,1");
    CHECK(g.stacks == 3 && !g.extended && g.autoFix);
}

// ---- big systems stay fast --------------------------------------------------------------------------
static void testLarge() {
    Config c = base(200, 999'999);
    c.collision = true;
    Plan p = build(c, 0, 0);
    CHECK(p.error.empty());
    CHECK(p.objs.size() > 2000);
    CHECK(validate(p).empty());
    CHECK(toLevelString(p).size() > 50000);
    Config d = base(60, 1'000'000'000);
    d.extended = true;
    Plan q = build(d, 0, 0);
    CHECK(q.error.empty() && validate(q).empty());
}

int main() {
    testNormal();
    testOmega();
    testCounters();
    testExtended();
    testIds();
    testValidate();
    testSerialize();
    testLarge();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
