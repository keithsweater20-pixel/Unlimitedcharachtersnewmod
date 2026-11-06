// Pickup Stack generator: adds a button to the level editor that opens a menu and, on "Create",
// builds the whole Pickup-trigger counter system automatically.
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
        bool collision = false; // clicks come from Collision trigger + Collision blocks
        int blockStart = 1;     // first Block ID used by collision blocks
    };

    std::string levelKey() {
        auto lel = LevelEditorLayer::get();
        std::string name = (lel && lel->m_level) ? std::string(lel->m_level->m_levelName) : std::string("unknown");
        return "stacks-" + name;
    }

    // Saved per level (by name) in the mod's save data.
    void saveConfig(StackConfig const& c) {
        Mod::get()->setSavedValue<std::string>(levelKey(),
            fmt::format("{},{},{},{},{},{},{},{},{}", c.stacks, c.clicks, c.omega ? 1 : 0, c.itemStart,
                c.groupStart, c.carry ? 1 : 0, c.perClick, c.collision ? 1 : 0, c.blockStart));
    }

    StackConfig loadConfig() {
        StackConfig c;
        auto s = Mod::get()->getSavedValue<std::string>(levelKey(), "");
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
        if (v.size() >= 9) { c.collision = v[7] != 0; c.blockStart = std::max<long long>(1, v[8]); }
        return c;
    }

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

    // ---- level-string builders (standard GD object string format) ----
    struct Builder {
        std::string out;
        void add(int objID, float px, float py, std::string const& props) {
            out += fmt::format("1,{},2,{},3,{}{};", objID, px, py, props);
        }
        void text(float px, float py, std::string const& txt) {
            add(914, px, py, fmt::format(",31,{},32,0.5", base64Url(txt)));
        }
        void display(float px, float py, int item) { add(1615, px, py, fmt::format(",80,{}", item)); }
    };

    // Object IDs: 1817 Pickup, 1611 Count, 1049 Toggle, 1595 Touch, 1815 Collision trigger,
    // 1816 Collision block, 1615 Item display, 914 Text, 1 plain block.
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

    // The click source: a Touch trigger, or a Collision trigger (Block A = block ID, Block B = Player 1).
    void clickSource(Builder& b, StackConfig const& c, float x, float y, int blockID, int spawnGroup, int actGroup) {
        if (c.collision) {
            b.add(1815, x, y, fmt::format(",80,{},138,1,51,{},56,1,57,{}", blockID, spawnGroup, actGroup));
        } else {
            b.add(1595, x, y, fmt::format(",51,{},57,{}", spawnGroup, actGroup));
        }
    }

    // The block that disappears: a Collision block with its own Block ID in collision mode, else a plain block.
    void blockObject(Builder& b, StackConfig const& c, float x, float y, int blockID, int group) {
        if (c.collision) b.add(1816, x, y, fmt::format(",80,{},57,{}", blockID, group));
        else b.add(1, x, y, fmt::format(",57,{}", group));
    }

    CCPoint basePoint(LevelEditorLayer* lel) {
        auto win = CCDirector::get()->getWinSize();
        CCPoint center = lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
        return ccp(std::floor(center.x / 30.f) * 30.f + 15.f, std::floor(center.y / 30.f) * 30.f + 15.f);
    }

    std::string generateCarry(StackConfig const& c, LevelEditorLayer* lel) {
        int n = c.stacks;
        if (c.perClick < 1 || c.perClick >= c.clicks) return "Add Per Click must be at least 1 and smaller than the base.";
        if (c.perClick > 100) return "Add Per Click is limited to 100 in Carry Mode.";
        if (c.itemStart + n - 1 > kMaxID) return "Item IDs would go above 9999.";
        if (c.groupStart + n + 1 > kMaxID) return "Group IDs would go above 9999.";
        int CLICK = c.groupStart;
        auto END = [&](int i) { return c.groupStart + 1 + i; };
        int BLK = c.groupStart + 1 + n;
        auto ITEM = [&](int i) { return c.itemStart + i; };

        auto p = basePoint(lel);
        float bx = p.x, by = p.y;
        Builder b;
        clickSource(b, c, bx, by, c.blockStart, CLICK, 0);
        blockObject(b, c, bx, by - 30.f, c.blockStart, BLK);
        b.text(bx + 60.f, by, "Click");
        for (int k = 0; k < static_cast<int>(c.perClick); ++k)
            b.add(1817, bx + 30.f + 30.f * k, by + 0.f, pickup(ITEM(0), 1, false, CLICK));
        for (int i = 0; i < n; ++i) {
            float y = by + (i + 1) * 30.f;
            if (i == n - 1) {
                // top digit: when full, hold at base-1... and make the block disappear (counter full)
                b.add(1611, bx, y, fmt::format(",80,{},77,{},51,{},56,1,104,1", ITEM(i), c.clicks, END(i)));
                b.add(1817, bx + 30.f, y, pickup(ITEM(i), c.clicks - 1, true, END(i)));
                b.add(1049, bx + 60.f, y, toggle(BLK, false, END(i)));
            } else {
                b.add(1611, bx, y, fmt::format(",80,{},77,{},51,{},56,1,104,1", ITEM(i), c.clicks, END(i)));
                b.add(1817, bx + 30.f, y, pickup(ITEM(i), 0, true, END(i)));
                b.add(1817, bx + 60.f, y, pickup(ITEM(i + 1), 1, false, END(i)));
            }
            b.display(bx + 100.f, y, ITEM(i));
            b.text(bx + 150.f, y, fmt::format("Stack {}", i + 1));
        }
        lel->createObjectsFromString(b.out, false, false);
        saveConfig(c);
        return fmt::format("Carry counter: {} digits, base {}, items {}-{}, groups {}-{}.",
            n, c.clicks, ITEM(0), ITEM(n - 1), c.groupStart, BLK);
    }

    std::string generate(StackConfig const& c) {
        auto lel = LevelEditorLayer::get();
        if (!lel) return "Open the level editor first.";
        int n = c.stacks;
        if (c.blockStart + (c.carry ? 0 : n - 1) > kMaxID) return "Block IDs would go above 9999.";
        if (c.carry) return generateCarry(c, lel);
        if (c.itemStart + n - 1 > kMaxID) return "Item IDs would go above 9999.";
        if (c.groupStart + n * 4 - 1 > kMaxID) return "Group IDs would go above 9999.";

        auto ACT = [&](int i) { return c.groupStart + i * 4; };     // click source
        auto PK  = [&](int i) { return c.groupStart + i * 4 + 1; }; // +1 pickup
        auto END = [&](int i) { return c.groupStart + i * 4 + 2; }; // runs when the stack is full
        auto BLK = [&](int i) { return c.groupStart + i * 4 + 3; }; // the block that disappears
        auto ITEM = [&](int i) { return c.itemStart + i; };
        auto BID = [&](int i) { return c.blockStart + i; };

        auto p = basePoint(lel);
        float bx = p.x, by = p.y;

        Builder b;
        for (int i = 0; i < n; ++i) {
            float y = by + i * 30.f;
            clickSource(b, c, bx, y, BID(i), PK(i), ACT(i));                          // click source
            b.add(1817, bx + 30.f, y, pickup(ITEM(i), 1, false, PK(i)));              // +1
            b.add(1611, bx + 60.f, y, fmt::format(",80,{},77,{},51,{},56,1,104,1", ITEM(i), c.clicks, END(i)));
            b.add(1817, bx + 90.f, y, pickup(ITEM(i), 0, true, END(i)));              // reset to 0
            b.add(1049, bx + 120.f, y, toggle(BLK(i), false, END(i)));                // block disappears
            if (n > 1) {
                int next = (i + 1) % n;
                b.add(1049, bx + 150.f, y, toggle(ACT(i), false, END(i)));            // this stack off
                b.add(1049, bx + 180.f, y, toggle(ACT(next), true, END(i)));          // next stack on
                b.add(1049, bx + 210.f, y, toggle(BLK(next), true, END(i)));          // next block appears
            }
            blockObject(b, c, bx + 240.f, y, BID(i), BLK(i));                         // the block
            b.display(bx + 280.f, y, ITEM(i));                                        // live counter (shows the reset)
            b.text(bx + 340.f, y, fmt::format("Stack {}", i + 1));                    // label
        }
        for (int i = 1; i < n; ++i)
            b.add(1049, bx + (i - 1) * 30.f, by - 30.f, fmt::format(",51,{},56,0", ACT(i)));

        lel->createObjectsFromString(b.out, false, false);
        saveConfig(c);
        return fmt::format("Created {} stacks: items {}-{}, groups {}-{}{}.", n, ITEM(0), ITEM(n - 1),
            c.groupStart, c.groupStart + n * 4 - 1,
            c.collision ? fmt::format(", block IDs {}-{}", BID(0), BID(n - 1)) : std::string());
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
    TextInput* m_blockIn = nullptr;
    CCMenuItemToggler* m_omega = nullptr;
    CCMenuItemToggler* m_carry = nullptr;
    CCMenuItemToggler* m_collision = nullptr;
    StackConfig m_cfg;

    CCPoint toMenu(CCPoint p) {
        return m_buttonMenu->convertToNodeSpace(m_mainLayer->convertToWorldSpace(p));
    }

    bool init() {
        if (!Popup::init(400.f, 290.f)) return false;
        this->setTitle("Pickup Stacks");
        m_cfg = loadConfig();
        auto size = m_mainLayer->getContentSize();

        auto addField = [&](char const* label, TextInput*& field, std::string value, float labelX, float fieldX, float y) {
            auto l = CCLabelBMFont::create(label, "bigFont.fnt");
            l->setScale(.33f);
            l->setAnchorPoint({0.f, .5f});
            l->setPosition({labelX, y});
            m_mainLayer->addChild(l);
            field = TextInput::create(85.f, "0", "bigFont.fnt");
            field->setFilter("0123456789");
            field->setMaxCharCount(10);
            field->setString(value);
            field->setPosition({fieldX, y});
            m_mainLayer->addChild(field);
        };
        float y0 = size.height - 60.f, step = 38.f;
        addField("Stack Count", m_stacksIn, std::to_string(m_cfg.stacks), 15.f, 160.f, y0);
        addField("Click Requirement", m_clicksIn, std::to_string(m_cfg.clicks), 15.f, 160.f, y0 - step);
        addField("Start Item ID", m_itemIn, std::to_string(m_cfg.itemStart), 15.f, 160.f, y0 - 2 * step);
        addField("Start Group ID", m_groupIn, std::to_string(m_cfg.groupStart), 215.f, 345.f, y0);
        addField("Add Per Click", m_perIn, std::to_string(m_cfg.perClick), 215.f, 345.f, y0 - step);
        addField("Start Block ID", m_blockIn, std::to_string(m_cfg.blockStart), 215.f, 345.f, y0 - 2 * step);

        float ty = y0 - 3 * step - 4.f;
        auto addToggle = [&](char const* label, CCMenuItemToggler*& tog, bool value, float y) {
            auto l = CCLabelBMFont::create(label, "bigFont.fnt");
            l->setScale(.36f);
            l->setAnchorPoint({0.f, .5f});
            l->setPosition({15.f, y});
            m_mainLayer->addChild(l);
            tog = CCMenuItemExt::createToggler(
                CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
                CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
                [](CCMenuItemToggler*) {}
            );
            tog->toggle(value);
            tog->setScale(.6f);
            tog->setPosition(toMenu({300.f, y}));
            m_buttonMenu->addChild(tog);
        };
        addToggle("Omega Mode (to 2147483647)", m_omega, m_cfg.omega, ty);
        addToggle("Carry Mode (digits)", m_carry, m_cfg.carry, ty - 28.f);
        addToggle("Collision Mode (collision blocks)", m_collision, m_cfg.collision, ty - 56.f);

        auto btn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Create"),
            [this](CCMenuItemSpriteExtra*) { this->onCreate(); }
        );
        btn->setPosition(toMenu({size.width - 60.f, 40.f}));
        m_buttonMenu->addChild(btn);
        return true;
    }

    void onCreate() {
        StackConfig c;
        bool omega = m_omega->isToggled();
        c.omega = omega;
        c.carry = m_carry->isToggled();
        c.collision = m_collision->isToggled();
        c.perClick = std::max<int64_t>(1, parse(m_perIn, 1));
        c.stacks = static_cast<int>(std::clamp<int64_t>(parse(m_stacksIn, 3), 1, kMaxStacks));
        c.clicks = std::clamp<int64_t>(parse(m_clicksIn, kNormalCap), 1, omega ? kOmegaCap : kNormalCap);
        c.itemStart = static_cast<int>(std::clamp<int64_t>(parse(m_itemIn, 1), 1, kMaxID));
        c.groupStart = static_cast<int>(std::clamp<int64_t>(parse(m_groupIn, 1), 1, kMaxID));
        c.blockStart = static_cast<int>(std::clamp<int64_t>(parse(m_blockIn, 1), 1, kMaxID));
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
