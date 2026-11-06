// Pickup Stack generator: adds a button to the level editor that opens a menu and, on "Create",
// builds the whole Pickup-trigger counter system automatically (see README for the layout).
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr int64_t kNormalCap = 999'999;
    constexpr int64_t kOmegaCap = INT_MAX; // 2,147,483,647
    constexpr int kMaxStacks = 200;
    constexpr int kMaxID = 9999;

    struct StackConfig {
        int stacks = 3;
        int64_t clicks = 999'999;
        bool omega = false;
        int itemStart = 1;
        int groupStart = 1;
        bool carry = false;     // odometer mode: stacks are digits, clicks = base
        int64_t perClick = 1;   // amount added to the first stack per click
    };

    std::string levelKey() {
        auto lel = LevelEditorLayer::get();
        std::string name = (lel && lel->m_level) ? std::string(lel->m_level->m_levelName) : std::string("unknown");
        return "stacks-" + name;
    }

    // Saved per level (by name) in the mod's save data: "stacks,clicks,omega,item,group"
    void saveConfig(StackConfig const& c) {
        Mod::get()->setSavedValue<std::string>(levelKey(),
            fmt::format("{},{},{},{},{},{},{}", c.stacks, c.clicks, c.omega ? 1 : 0, c.itemStart, c.groupStart, c.carry ? 1 : 0, c.perClick));
    }

    StackConfig loadConfig() {
        StackConfig c;
        auto s = Mod::get()->getSavedValue<std::string>(levelKey(), "");
        if (s.empty()) return c;
        std::vector<long long> v;
        std::stringstream ss(s);
        std::string part;
        while (std::getline(ss, part, ',')) v.push_back(std::atoll(part.c_str()));
        if (v.size() == 5 || v.size() == 7) {
            c.stacks = static_cast<int>(v[0]);
            c.clicks = v[1];
            c.omega = v[2] != 0;
            c.itemStart = static_cast<int>(v[3]);
            c.groupStart = static_cast<int>(v[4]);
            if (v.size() == 7) { c.carry = v[5] != 0; c.perClick = std::max<long long>(1, v[6]); }
        }
        return c;
    }

    // ---- level-string builders (standard GD object string format) ----
    struct Builder {
        std::string out;
        float x, y;

        void add(int objID, float px, float py, std::string const& props) {
            out += fmt::format("1,{},2,{},3,{}{};", objID, px, py, props);
        }
    };

    // Trigger object IDs: 1817 Pickup, 1611 Count, 1049 Toggle, 1595 Touch.
    std::string spawnFlags() { return ",62,1,87,1"; }

    std::string pickup(int item, int64_t count, bool override_, int group) {
        std::string s = fmt::format(",80,{},77,{}", item, count);
        if (override_) s += ",139,1";
        s += fmt::format(",57,{}", group);
        s += spawnFlags();
        return s;
    }

    std::string toggle(int target, bool on, int group) {
        return fmt::format(",51,{},56,{},57,{}", target, on ? 1 : 0, group) + spawnFlags();
    }

    std::string generateCarry(StackConfig const& c, LevelEditorLayer* lel) {
        int n = c.stacks;
        if (c.perClick < 1 || c.perClick >= c.clicks) return "Add Per Click must be at least 1 and smaller than the base.";
        if (c.perClick > 100) return "Add Per Click is limited to 100 in Carry Mode.";
        if (c.itemStart + n - 1 > kMaxID) return "Item IDs would go above 9999.";
        if (c.groupStart + n - 1 > kMaxID) return "Group IDs would go above 9999.";
        auto CLICK = c.groupStart;                       // group run on every click
        auto END = [&](int i) { return c.groupStart + 1 + i; }; // digit i overflowed
        auto ITEM = [&](int i) { return c.itemStart + i; };

        auto win = CCDirector::get()->getWinSize();
        CCPoint center = lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
        float bx = std::floor(center.x / 30.f) * 30.f + 15.f;
        float by = std::floor(center.y / 30.f) * 30.f + 15.f;

        Builder b;
        // Click catcher: every click spawns the +perClick pickup on digit 0.
        b.add(1595, bx, by, fmt::format(",51,{}", CLICK));
        // perClick separate +1 pickups (instead of one +N) so the Count trigger sees every
        // value and no divisibility maths is needed.
        for (int k = 0; k < static_cast<int>(c.perClick); ++k)
            b.add(1817, bx + 30.f + 30.f * k, by, pickup(ITEM(0), 1, false, CLICK));
        for (int i = 0; i < n; ++i) {
            float y = by + (i + 1) * 30.f;
            if (i == n - 1) {
                // top digit: stop at base so it cannot overflow (counter is full)
                b.add(1611, bx, y, fmt::format(",80,{},77,{},51,{},56,1,104,1", ITEM(i), c.clicks, END(i)));
                b.add(1817, bx + 30.f, y, pickup(ITEM(i), c.clicks - 1, true, END(i)));
                continue;
            }
            // when digit i reaches the base: reset it to 0 and carry +1 into digit i+1
            b.add(1611, bx, y, fmt::format(",80,{},77,{},51,{},56,1,104,1", ITEM(i), c.clicks, END(i)));
            b.add(1817, bx + 30.f, y, pickup(ITEM(i), 0, true, END(i)));
            b.add(1817, bx + 60.f, y, pickup(ITEM(i + 1), 1, false, END(i)));
        }
        lel->createObjectsFromString(b.out, false, false);
        saveConfig(c);
        return fmt::format("Carry counter: {} digits, base {}, items {}-{}, groups {}-{}.",
            n, c.clicks, ITEM(0), ITEM(n - 1), c.groupStart, END(n - 1));
    }

    // Builds and places everything. Returns a status string for the notification.
    std::string generate(StackConfig const& c) {
        auto lel = LevelEditorLayer::get();
        if (!lel) return "Open the level editor first.";

        int n = c.stacks;
        if (c.carry) return generateCarry(c, lel);
        int groupsNeeded = n * 3;
        if (c.itemStart + n - 1 > kMaxID) return "Item IDs would go above 9999.";
        if (c.groupStart + groupsNeeded - 1 > kMaxID) return "Group IDs would go above 9999.";

        auto ACT = [&](int i) { return c.groupStart + i * 3; };     // touch trigger (click catcher)
        auto PK  = [&](int i) { return c.groupStart + i * 3 + 1; }; // +1 pickup
        auto END = [&](int i) { return c.groupStart + i * 3 + 2; }; // runs when stack is full
        auto ITEM = [&](int i) { return c.itemStart + i; };

        // Place at the middle of the editor view, snapped to the 30-unit grid.
        auto win = CCDirector::get()->getWinSize();
        CCPoint center = lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
        float bx = std::floor(center.x / 30.f) * 30.f + 15.f;
        float by = std::floor(center.y / 30.f) * 30.f + 15.f;

        Builder b;
        for (int i = 0; i < n; ++i) {
            float y = by + i * 30.f;
            // 1) click catcher: a touch trigger that spawns this stack's pickup group
            b.add(1595, bx, y, fmt::format(",51,{},57,{}", PK(i), ACT(i)));
            // 2) +1 pickup on this stack's item
            b.add(1817, bx + 30.f, y, pickup(ITEM(i), 1, false, PK(i)));
            // 3) watcher: when item == clicks, spawn the END group (multi-activate so it re-arms)
            b.add(1611, bx + 60.f, y, fmt::format(",80,{},77,{},51,{},56,1,104,1", ITEM(i), c.clicks, END(i)));
            // 4) END group: reset this item to 0
            b.add(1817, bx + 90.f, y, pickup(ITEM(i), 0, true, END(i)));
            // 5) END group: switch click catcher to the next stack (wraps around)
            if (n > 1) {
                int next = (i + 1) % n;
                b.add(1049, bx + 120.f, y, toggle(ACT(i), false, END(i)));
                b.add(1049, bx + 150.f, y, toggle(ACT(next), true, END(i)));
            }
        }
        // Start-of-level setup: only stack 1 listens for clicks.
        for (int i = 1; i < n; ++i) {
            b.add(1049, bx + (i - 1) * 30.f, by - 30.f, fmt::format(",51,{},56,0", ACT(i)));
        }

        lel->createObjectsFromString(b.out, false, false);
        saveConfig(c);
        return fmt::format("Created {} stacks: items {}-{}, groups {}-{}.",
            n, ITEM(0), ITEM(n - 1), c.groupStart, c.groupStart + groupsNeeded - 1);
    }

    int64_t parse(TextInput* in, int64_t fallback) {
        auto s = std::string(in->getString());
        if (s.empty()) return fallback;
        return std::strtoll(s.c_str(), nullptr, 10);
    }
}

class StackPopup : public Popup {
protected:
    TextInput* m_stacksIn = nullptr;
    TextInput* m_clicksIn = nullptr;
    TextInput* m_itemIn = nullptr;
    TextInput* m_groupIn = nullptr;
    TextInput* m_perIn = nullptr;
    CCMenuItemToggler* m_omega = nullptr;
    CCMenuItemToggler* m_carry = nullptr;
    StackConfig m_cfg;

    bool init() {
        if (!Popup::init(300.f, 340.f)) return false;
        this->setTitle("Pickup Stacks");
        m_cfg = loadConfig();

        auto size = m_mainLayer->getContentSize();
        float left = 20.f, inputX = size.width - 90.f;
        float y = size.height - 55.f;

        auto addRow = [&](char const* label, TextInput*& field, std::string value) {
            auto l = CCLabelBMFont::create(label, "bigFont.fnt");
            l->setScale(.45f);
            l->setAnchorPoint({0.f, .5f});
            l->setPosition({left, y});
            m_mainLayer->addChild(l);
            field = TextInput::create(110.f, "0", "bigFont.fnt");
            field->setFilter("0123456789");
            field->setMaxCharCount(10);
            field->setString(value);
            field->setPosition({inputX, y});
            m_mainLayer->addChild(field);
            y -= 38.f;
        };
        addRow("Stack Count", m_stacksIn, std::to_string(m_cfg.stacks));
        addRow("Click Requirement", m_clicksIn, std::to_string(m_cfg.clicks));
        addRow("Start Item ID", m_itemIn, std::to_string(m_cfg.itemStart));
        addRow("Start Group ID", m_groupIn, std::to_string(m_cfg.groupStart));
        addRow("Add Per Click", m_perIn, std::to_string(m_cfg.perClick));

        auto addToggle = [&](char const* label, CCMenuItemToggler*& tog, bool value) {
            auto l = CCLabelBMFont::create(label, "bigFont.fnt");
            l->setScale(.4f);
            l->setAnchorPoint({0.f, .5f});
            l->setPosition({left, y});
            m_mainLayer->addChild(l);
            tog = CCMenuItemExt::createToggler(
                CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
                CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
                [](CCMenuItemToggler*) {}
            );
            tog->toggle(value);
            tog->setScale(.7f);
            tog->setPosition(m_buttonMenu->convertToNodeSpace(m_mainLayer->convertToWorldSpace({size.width - 40.f, y})));
            m_buttonMenu->addChild(tog);
            y -= 34.f;
        };
        addToggle("Carry Mode (digits)", m_carry, m_cfg.carry);

        addToggle("Omega Mode (to 2147483647)", m_omega, m_cfg.omega);

        auto btn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Create"),
            [this](CCMenuItemSpriteExtra*) { this->onCreate(); }
        );
        btn->setPosition(m_buttonMenu->convertToNodeSpace(m_mainLayer->convertToWorldSpace({size.width / 2, 28.f})));
        m_buttonMenu->addChild(btn);
        return true;
    }

    void onCreate() {
        StackConfig c;
        bool omega = m_omega->isToggled();
        c.omega = omega;
        c.carry = m_carry->isToggled();
        c.perClick = std::max<int64_t>(1, parse(m_perIn, 1));
        c.stacks = static_cast<int>(std::clamp<int64_t>(parse(m_stacksIn, 3), 1, kMaxStacks));
        c.clicks = std::clamp<int64_t>(parse(m_clicksIn, kNormalCap), 1, omega ? kOmegaCap : kNormalCap);
        c.itemStart = static_cast<int>(std::clamp<int64_t>(parse(m_itemIn, 1), 1, kMaxID));
        c.groupStart = static_cast<int>(std::clamp<int64_t>(parse(m_groupIn, 1), 1, kMaxID));
        auto msg = generate(c);
        Notification::create(msg.c_str(), NotificationIcon::Info, 3.f)->show();
        log::info("TpllKit stacks: {}", msg);
        this->onClose(nullptr);
    }

public:
    static StackPopup* create() {
        auto ret = new StackPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class $modify(TpllEditorUI, EditorUI) {
    bool init(LevelEditorLayer* layer) {
        if (!EditorUI::init(layer)) return false;
        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        auto spr = ButtonSprite::create("Stacks", 60, true, "bigFont.fnt", "GJ_button_04.png", 30.f, .6f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            if (auto p = StackPopup::create()) p->show();
        });
        btn->setPosition({win.width * 0.70f, win.height * 0.68f}); // left of the Copy button
        menu->addChild(btn);
        this->addChild(menu, 100);
        return true;
    }
};
