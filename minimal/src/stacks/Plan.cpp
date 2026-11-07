#include "Plan.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace tpll {

namespace {
template <class T>
std::string str(T v) {
    return std::to_string(v);
}

struct B {
    std::vector<Obj>& v;
    Obj& add(Kind k, float x, float y) {
        v.emplace_back();
        auto& o = v.back();
        o.kind = k;
        o.x = x;
        o.y = y;
        return o;
    }
    void pickup(float x, float y, int item, int64_t count, bool ov, int group) {
        auto& o = add(Kind::Pickup, x, y);
        o.item = item;
        o.count = count;
        o.override_ = ov;
        o.group = group;
        o.spawn = true;
    }
    void toggle(float x, float y, int target, bool on, int group, bool spawn) {
        auto& o = add(Kind::Toggle, x, y);
        o.target = target;
        o.on = on;
        o.group = group;
        o.spawn = spawn;
    }
    void count(float x, float y, int item, int64_t value, int target, int group) {
        auto& o = add(Kind::Count, x, y);
        o.item = item;
        o.count = value;
        o.target = target;
        o.group = group;
    }
    void display(float x, float y, int item) { add(Kind::Display, x, y).item = item; }
    void text(float x, float y, std::string const& t) { add(Kind::Text, x, y).text = t; }
    // The click source: a Touch trigger, or a Collision trigger (Block A = block ID, Block B = Player 1).
    void clickSource(Config const& c, float x, float y, int blockId, int spawnGroup, int actGroup) {
        if (c.collision) {
            auto& o = add(Kind::CollisionTrigger, x, y);
            o.blockId = blockId;
            o.target = spawnGroup;
            o.group = actGroup;
        } else {
            auto& o = add(Kind::Touch, x, y);
            o.target = spawnGroup;
            o.group = actGroup;
        }
    }
    // The block that disappears: a Collision block in collision mode, else a plain block.
    void blockObject(Config const& c, float x, float y, int blockId, int group) {
        if (c.collision) {
            auto& o = add(Kind::CollisionBlock, x, y);
            o.blockId = blockId;
            o.group = group;
        } else {
            add(Kind::Block, x, y).group = group;
        }
    }
};

bool rangeFree(std::set<int> const& used, int start, int count) {
    if (start < 1 || start + count - 1 > kMaxID) return false;
    for (int i = 0; i < count; ++i)
        if (used.count(start + i)) return false;
    return true;
}

bool pickFree(std::set<int> const& used, int from, int count, int& out) {
    if (count <= 0) return true;
    for (int s = std::max(1, from); s + count - 1 <= kMaxID; ++s)
        if (rangeFree(used, s, count)) { out = s; return true; }
    for (int s = 1; s < from && s + count - 1 <= kMaxID; ++s)
        if (rangeFree(used, s, count)) { out = s; return true; }
    return false;
}

void rangeConflicts(std::set<int> const& used, int start, int count, std::vector<int>& out) {
    for (int i = 0; i < count; ++i)
        if (used.count(start + i)) out.push_back(start + i);
}
}  // namespace

int64_t capFor(Config const& c) { return c.omega ? kOmegaCap : std::min<int64_t>(c.clicks, kNormalCap); }

Config sanitize(Config c) {
    c.stacks = std::clamp(c.stacks, 1, c.extended ? kMaxLimbs : kMaxStacks);
    if (c.extended) {
        c.clicks = std::clamp<int64_t>(c.clicks, 2, kOmegaCap);
        c.perClick = std::clamp<int64_t>(c.perClick, 1, std::min<int64_t>(100, c.clicks - 1));
    } else {
        c.clicks = std::clamp<int64_t>(c.clicks, 1, c.omega ? kOmegaCap : kNormalCap);
        c.perClick = std::max<int64_t>(1, c.perClick);
    }
    c.itemStart = std::clamp(c.itemStart, 1, kMaxID);
    c.groupStart = std::clamp(c.groupStart, 1, kMaxID);
    c.blockStart = std::clamp(c.blockStart, 1, kMaxID);
    return c;
}

Needs needsFor(Config const& c) {
    int n = c.stacks;
    Needs nd;
    nd.items = n;
    nd.blocks = c.collision ? (c.extended ? 1 : n) : 0;
    if (c.extended) nd.groups = n + 3;      // ACT, CLICK, STOP, BLK + (n-1) carry groups
    else if (c.carry) nd.groups = 4 * n + 2;
    else nd.groups = 4 * n;
    return nd;
}

Conflicts findConflicts(Config const& c, UsedIds const& used) {
    Conflicts r;
    auto nd = needsFor(c);
    rangeConflicts(used.groups, c.groupStart, nd.groups, r.groups);
    rangeConflicts(used.items, c.itemStart, nd.items, r.items);
    rangeConflicts(used.blocks, c.blockStart, nd.blocks, r.blocks);
    return r;
}

bool autoFixIds(Config& c, UsedIds const& used) {
    auto nd = needsFor(c);
    int g = c.groupStart, i = c.itemStart, b = c.blockStart;
    if (!pickFree(used.groups, c.groupStart, nd.groups, g)) return false;
    if (!pickFree(used.items, c.itemStart, nd.items, i)) return false;
    if (!pickFree(used.blocks, c.blockStart, nd.blocks, b)) return false;
    c.groupStart = g;
    c.itemStart = i;
    if (nd.blocks) c.blockStart = b;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Normal mode: N stacks, each fills to the click requirement, resets, and hands over to the next.
// ---------------------------------------------------------------------------------------------
static void buildNormal(Plan& p, float bx, float by) {
    Config const& c = p.cfg;
    int n = c.stacks;
    auto ACT = [&](int i) { return c.groupStart + i * 4; };      // click source
    auto PK = [&](int i) { return c.groupStart + i * 4 + 1; };   // +1 pickup
    auto END = [&](int i) { return c.groupStart + i * 4 + 2; };  // runs when the stack is full
    auto BLK = [&](int i) { return c.groupStart + i * 4 + 3; };  // the block that disappears
    auto ITEM = [&](int i) { return c.itemStart + i; };
    auto BID = [&](int i) { return c.blockStart + i; };
    B b{p.objs};
    int64_t cap = c.clicks;
    for (int i = 0; i < n; ++i) {
        float y = by + i * 30.f;
        b.clickSource(c, bx, y, BID(i), PK(i), ACT(i));
        b.pickup(bx + 30.f, y, ITEM(i), 1, false, PK(i));
        b.count(bx + 60.f, y, ITEM(i), cap, END(i), 0);
        b.pickup(bx + 90.f, y, ITEM(i), 0, true, END(i));
        b.toggle(bx + 120.f, y, BLK(i), false, END(i), true);
        if (n > 1) {
            int next = (i + 1) % n;
            b.toggle(bx + 150.f, y, ACT(i), false, END(i), true);
            b.toggle(bx + 180.f, y, ACT(next), true, END(i), true);
            b.toggle(bx + 210.f, y, BLK(next), true, END(i), true);
        }
        b.blockObject(c, bx + 240.f, y, BID(i), BLK(i));
        b.display(bx + 280.f, y, ITEM(i));
        b.text(bx + 340.f, y, "Stack " + str(i + 1));
    }
    for (int i = 1; i < n; ++i) b.toggle(bx + (i - 1) * 30.f, by - 30.f, ACT(i), false, 0, false);
    p.groupFirst = c.groupStart;
    p.groupLast = c.groupStart + n * 4 - 1;
    p.itemFirst = ITEM(0);
    p.itemLast = ITEM(n - 1);
    if (c.collision) { p.blockFirst = BID(0); p.blockLast = BID(n - 1); }
    p.summary = "Stacks x" + str(n) + " | clicks " + str(cap);
}

// ---------------------------------------------------------------------------------------------
// Counter Mode: N counters side by side. Counter 1 takes the clicks; at the cap it resets to 0
// and the first counter that is not full yet gets +1. When every counter is full the click
// source is switched off.
// ---------------------------------------------------------------------------------------------
static void buildCounters(Plan& p, float bx, float by) {
    Config const& c = p.cfg;
    int n = c.stacks;
    int64_t cap = capFor(c);
    int g = c.groupStart;
    int CLICK = g, RG = g + 1, RR = g + 2, FIN = g + 3;
    auto G = [&](int s) { return g + 4 + (s - 2); };            // s = 2..n+1
    auto CARRY = [&](int s) { return g + 4 + n + (s - 2); };    // s = 2..n
    auto STG = [&](int s) { return g + 3 + 2 * n + (s - 2); };  // s = 2..n
    auto BLK = [&](int i) { return g + 3 * n + 2 + (i - 1); };  // i = 1..n
    auto ITEM = [&](int i) { return c.itemStart + i - 1; };     // i = 1..n
    auto BID = [&](int i) { return c.blockStart + i - 1; };
    B b{p.objs};

    b.clickSource(c, bx, by, BID(1), CLICK, 0);
    b.text(bx, by + 30.f, "Click");
    for (int k = 0; k < static_cast<int>(c.perClick); ++k) b.pickup(bx + 30.f * k, by - 60.f, ITEM(1), 1, false, CLICK);
    b.count(bx, by + 90.f, ITEM(1), cap, RG, RR);
    for (int s = 2; s <= n + 1; ++s) {
        int target = (s <= n) ? CARRY(s) : FIN;
        b.count(bx + 30.f * (s - 1), by + 90.f, ITEM(1), cap, target, G(s));
    }
    // start-of-level setup: only the first carry route is active
    for (int s = 3; s <= n + 1; ++s) b.toggle(bx + 30.f * (s - 3), by + 120.f, G(s), false, 0, false);
    if (n == 1) b.toggle(bx, by + 150.f, RR, false, 0, false);

    for (int i = 1; i <= n; ++i) {
        float cx = bx + 150.f + (i - 1) * 150.f;
        b.display(cx, by, ITEM(i));
        b.text(cx, by + 30.f, "Counter " + str(i));
        b.blockObject(c, cx, by - 30.f, BID(i), BLK(i));
        if (i == 1) {
            b.pickup(cx + 30.f, by, ITEM(1), 0, true, RG);        // reset to 0
            b.toggle(cx + 60.f, by, CLICK, false, FIN, true);     // everything full: stop clicks
            b.toggle(cx + 90.f, by, BLK(1), false, FIN, true);    // counter 1 block disappears
        } else {
            b.pickup(cx + 30.f, by, ITEM(i), 1, false, CARRY(i));  // +1 from counter 1
            b.count(cx + 60.f, by, ITEM(i), cap, STG(i), 0);
            b.toggle(cx + 60.f, by + 30.f, G(i), false, STG(i), true);      // stop routing to this counter
            b.toggle(cx + 90.f, by + 30.f, G(i + 1), true, STG(i), true);   // route to the next one
            b.toggle(cx + 60.f, by + 60.f, BLK(i), false, STG(i), true);    // its block disappears
            if (i == n) b.toggle(cx + 90.f, by + 60.f, RR, false, STG(i), true);  // no more resets
        }
    }
    p.groupFirst = g;
    p.groupLast = g + 4 * n + 1;
    p.itemFirst = ITEM(1);
    p.itemLast = ITEM(n);
    if (c.collision) { p.blockFirst = BID(1); p.blockLast = BID(n); }
    p.summary = "Counters x" + str(n) + " | cap " + str(cap);
}

// ---------------------------------------------------------------------------------------------
// Experimental: Extended Integer Mode. A positional multi-limb number in base B. Limb 0 takes
// the clicks. Each limb has a Count trigger that fires when the limb reaches B: it resets that
// limb to 0 and adds 1 to the next limb, whose own Count trigger carries on the same way. When
// the top limb reaches B the number cannot grow any more: every limb is set to B-1 (the largest
// value) and the click source is switched off, so nothing ever wraps to zero silently.
// ---------------------------------------------------------------------------------------------
static void buildExtended(Plan& p, float bx, float by) {
    Config const& c = p.cfg;
    int k = c.stacks;           // limbs
    int64_t base = c.clicks;    // limb base
    int g = c.groupStart;
    int ACT = g, CLICK = g + 1, STOP = g + 2, BLK = g + 3;
    auto CARRY = [&](int i) { return g + 4 + i; };  // i = 0..k-2
    auto ITEM = [&](int i) { return c.itemStart + i; };
    B b{p.objs};

    b.clickSource(c, bx, by, c.blockStart, CLICK, ACT);
    b.text(bx, by + 30.f, "Click");
    b.blockObject(c, bx, by - 30.f, c.blockStart, BLK);
    for (int q = 0; q < static_cast<int>(c.perClick); ++q) b.pickup(bx + 30.f * q, by - 60.f, ITEM(0), 1, false, CLICK);

    for (int i = 0; i < k; ++i) {
        // most significant limb on the left
        float x = bx + 150.f + (k - 1 - i) * 150.f;
        int target = (i < k - 1) ? CARRY(i) : STOP;
        b.count(bx + 30.f * i, by + 90.f, ITEM(i), base, target, 0);
        b.display(x, by, ITEM(i));
        b.text(x, by + 30.f, "Limb " + str(i + 1) + (i == k - 1 ? " (top)" : (i == 0 ? " (ones)" : "")));
        if (i < k - 1) {
            b.pickup(x + 30.f, by, ITEM(i), 0, true, CARRY(i));       // this limb back to 0
            b.pickup(x + 60.f, by, ITEM(i + 1), 1, false, CARRY(i));  // +1 on the next limb
        }
    }
    // Top limb is full: saturate and stop.
    float sx = bx + 150.f;
    b.toggle(sx, by + 60.f, ACT, false, STOP, true);
    b.toggle(sx + 30.f, by + 60.f, BLK, false, STOP, true);
    for (int i = 0; i < k; ++i) b.pickup(sx + 60.f + 30.f * i, by + 60.f, ITEM(i), base - 1, true, STOP);

    p.groupFirst = g;
    p.groupLast = g + 3 + (k - 1);
    p.itemFirst = ITEM(0);
    p.itemLast = ITEM(k - 1);
    if (c.collision) p.blockFirst = p.blockLast = c.blockStart;
    p.summary = "Extended x" + str(k) + " limbs | base " + str(base);
}

Plan build(Config c, float bx, float by) {
    Plan p;
    c = sanitize(c);
    p.cfg = c;
    int n = c.stacks;
    int64_t cap = capFor(c);
    auto nd = needsFor(c);

    if (c.collision && c.blockStart + nd.blocks - 1 > kMaxID) { p.error = "Block IDs would go above 9999."; return p; }
    if (c.itemStart + nd.items - 1 > kMaxID) { p.error = "Item IDs would go above 9999."; return p; }
    if (c.groupStart + nd.groups - 1 > kMaxID) { p.error = "Group IDs would go above 9999."; return p; }
    if (!c.extended && c.carry) {
        if (cap < 2) { p.error = "Click Requirement must be at least 2 in Counter Mode."; return p; }
        if (c.perClick < 1 || c.perClick >= cap) { p.error = "Add Per Click must be at least 1 and smaller than the cap."; return p; }
        if (c.perClick > 100) { p.error = "Add Per Click is limited to 100 in Counter Mode."; return p; }
    }
    if (c.extended && c.perClick > 100) { p.error = "Add Per Click is limited to 100."; return p; }
    (void)n;

    if (c.extended) buildExtended(p, bx, by);
    else if (c.carry) buildCounters(p, bx, by);
    else buildNormal(p, bx, by);

    // One text object that lists every ID the system uses, so it is easy to find and edit later.
    std::string info = p.summary + " | items " + str(p.itemFirst) + "-" + str(p.itemLast) + " | groups " +
                       str(p.groupFirst) + "-" + str(p.groupLast);
    if (c.collision) info += " | blocks " + str(p.blockFirst) + "-" + str(p.blockLast);
    B{p.objs}.text(bx, by - 120.f, info);
    return p;
}

// ---------------------------------------------------------------------------------------------
std::vector<std::string> validate(Plan const& p) {
    std::vector<std::string> out;
    std::set<int> owned, spawned;
    std::set<int> written, watched;
    for (auto const& o : p.objs) {
        if (o.group > 0) owned.insert(o.group);
        switch (o.kind) {
            case Kind::Touch:
            case Kind::CollisionTrigger:
            case Kind::Count:
                spawned.insert(o.target);
                break;
            default: break;
        }
        if (o.kind == Kind::Pickup) written.insert(o.item);
        if (o.kind == Kind::Count) watched.insert(o.item);
    }
    for (auto const& o : p.objs) {
        switch (o.kind) {
            case Kind::Toggle:
                if (!owned.count(o.target)) out.push_back("Toggle targets group " + str(o.target) + " that nothing owns");
                if (o.spawn && (o.group <= 0 || !spawned.count(o.group)))
                    out.push_back("Toggle in group " + str(o.group) + " is never spawned");
                break;
            case Kind::Pickup:
                if (o.group <= 0 || !spawned.count(o.group))
                    out.push_back("Pickup on item " + str(o.item) + " in group " + str(o.group) + " is never spawned");
                break;
            case Kind::Touch:
            case Kind::CollisionTrigger:
            case Kind::Count:
                if (!owned.count(o.target)) out.push_back("Trigger spawns group " + str(o.target) + " that nothing owns");
                if (o.kind == Kind::Count && !written.count(o.item))
                    out.push_back("Count watches item " + str(o.item) + " that nothing writes");
                break;
            case Kind::Display:
                if (!written.count(o.item)) out.push_back("Display shows item " + str(o.item) + " that nothing writes");
                break;
            default: break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
namespace {
std::string base64Url(std::string const& in) {
    static char const* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        out += tbl[v >> 18 & 63]; out += tbl[v >> 12 & 63]; out += tbl[v >> 6 & 63]; out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = (uint8_t)in[i] << 16;
        out += tbl[v >> 18 & 63]; out += tbl[v >> 12 & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8;
        out += tbl[v >> 18 & 63]; out += tbl[v >> 12 & 63]; out += tbl[v >> 6 & 63]; out += '=';
    }
    return out;
}

// Object IDs: 1817 Pickup, 1611 Count, 1049 Toggle, 1595 Touch, 1815 Collision trigger,
// 1816 Collision block, 1615 Item display, 914 Text, 1 plain block.
std::string num(float v) {
    if (v == std::floor(v)) return std::to_string(static_cast<long long>(v));
    std::ostringstream ss;
    ss << std::setprecision(9) << v;
    return ss.str();
}
}  // namespace

std::string toLevelString(Plan const& p) {
    std::string out;
    out.reserve(p.objs.size() * 48);
    auto add = [&](int id, float x, float y, std::string const& props) {
        out += "1," + str(id) + ",2," + num(x) + ",3," + num(y) + props + ";";
    };
    char const* spawnFlags = ",62,1,87,1";
    for (auto const& o : p.objs) {
        switch (o.kind) {
            case Kind::Pickup: {
                std::string s = ",80," + str(o.item) + ",77," + str(o.count);
                if (o.override_) s += ",139,1";
                s += ",57," + str(o.group);
                s += spawnFlags;
                add(1817, o.x, o.y, s);
                break;
            }
            case Kind::Count: {
                std::string s = ",80," + str(o.item) + ",77," + str(o.count) + ",51," + str(o.target) + ",56,1,104,1";
                if (o.group > 0) s += ",57," + str(o.group);
                add(1611, o.x, o.y, s);
                break;
            }
            case Kind::Toggle: {
                std::string s = ",51," + str(o.target) + ",56," + (o.on ? "1" : "0");
                if (o.group > 0) s += ",57," + str(o.group);
                if (o.spawn) s += spawnFlags;
                add(1049, o.x, o.y, s);
                break;
            }
            case Kind::Touch:
                add(1595, o.x, o.y, ",51," + str(o.target) + ",57," + str(o.group));
                break;
            case Kind::CollisionTrigger:
                add(1815, o.x, o.y, ",80," + str(o.blockId) + ",138,1,51," + str(o.target) + ",56,1,57," + str(o.group));
                break;
            case Kind::CollisionBlock:
                add(1816, o.x, o.y, ",80," + str(o.blockId) + ",57," + str(o.group));
                break;
            case Kind::Block:
                add(1, o.x, o.y, ",57," + str(o.group));
                break;
            case Kind::Display:
                add(1615, o.x, o.y, ",80," + str(o.item));
                break;
            case Kind::Text:
                add(914, o.x, o.y, ",31," + base64Url(o.text) + ",32,0.5");
                break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
std::string encodeConfig(Config const& c) {
    std::ostringstream ss;
    ss << c.stacks << ',' << c.clicks << ',' << (c.omega ? 1 : 0) << ',' << c.itemStart << ',' << c.groupStart << ','
       << (c.carry ? 1 : 0) << ',' << c.perClick << ',' << (c.collision ? 1 : 0) << ',' << c.blockStart << ','
       << (c.extended ? 1 : 0) << ',' << (c.autoFix ? 1 : 0);
    return ss.str();
}

Config decodeConfig(std::string const& s) {
    Config c;
    if (s.empty()) return c;
    std::vector<long long> v;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) v.push_back(std::atoll(part.c_str()));
    if (v.size() >= 5) {
        c.stacks = static_cast<int>(v[0]);
        c.clicks = v[1];
        c.omega = v[2] != 0;
        c.itemStart = static_cast<int>(v[3]);
        c.groupStart = static_cast<int>(v[4]);
    }
    if (v.size() >= 7) { c.carry = v[5] != 0; c.perClick = std::max<long long>(1, v[6]); }
    if (v.size() >= 9) { c.collision = v[7] != 0; c.blockStart = static_cast<int>(std::max<long long>(1, v[8])); }
    if (v.size() >= 10) c.extended = v[9] != 0;
    if (v.size() >= 11) c.autoFix = v[10] != 0;
    return c;
}

}  // namespace tpll
