// Pickup Stack generator UI: a button in the level editor opens a menu; "Create" builds the whole
// counter system (see Plan.cpp for what it builds) as one undo step.
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>

#include "LevelScan.hpp"
#include "Plan.hpp"

using namespace geode::prelude;

namespace {
    std::string levelKey() {
        auto lel = LevelEditorLayer::get();
        std::string name = (lel && lel->m_level) ? std::string(lel->m_level->m_levelName) : std::string("unknown");
        return "stacks-" + name;
    }

    // Saved per level (by name) in the mod's save data.
    void saveConfig(tpll::Config const& c) { Mod::get()->setSavedValue<std::string>(levelKey(), tpll::encodeConfig(c)); }

    tpll::Config loadConfig() {
        return tpll::decodeConfig(Mod::get()->getSavedValue<std::string>(levelKey(), ""));
    }

    // The middle of the screen, snapped to the 30-unit grid, in level coordinates.
    CCPoint basePoint(LevelEditorLayer* lel) {
        auto win = CCDirector::get()->getWinSize();
        CCPoint center = lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
        return ccp(std::floor(center.x / 30.f) * 30.f + 15.f, std::floor(center.y / 30.f) * 30.f + 15.f);
    }

    void toast(std::string const& msg, NotificationIcon icon = NotificationIcon::Info, float time = 3.f) {
        Notification::create(msg.c_str(), icon, time)->show();
    }

    int64_t parse(TextInput* in, int64_t fallback) {
        auto s = std::string(in->getString());
        if (s.empty()) return fallback;
        return std::strtoll(s.c_str(), nullptr, 10);
    }

    // Builds the system and adds it to the level as a single undo step. Returns a message.
    // `ok` is false when nothing was created.
    std::string generate(tpll::Config c, bool& ok) {
        ok = false;
        auto lel = LevelEditorLayer::get();
        if (!lel) return "Open the level editor first.";
        c = tpll::sanitize(c);

        // IDs the level already uses: never reuse them (that would tangle two systems together).
        auto used = tpll::scanUsedIds();
        auto conflicts = tpll::findConflicts(c, used);
        std::string note;
        if (conflicts.any()) {
            auto what = tpll::describeConflicts(conflicts);
            if (!c.autoFix) return "IDs already in use (" + what + "). Turn on Auto-fix IDs or pick other IDs.";
            if (!tpll::autoFixIds(c, used)) return "No free ID range left for this many stacks (in use: " + what + ").";
            note = " IDs moved to a free range.";
        }

        auto p = tpll::build(c, basePoint(lel).x, basePoint(lel).y);
        if (!p.error.empty()) return p.error;
        auto problems = tpll::validate(p);
        if (!problems.empty()) {
            log::error("TpllKit stacks: refusing to create a disconnected system: {}", problems.front());
            return "Internal check failed (" + problems.front() + "). Nothing was created.";
        }

        // noUndo = true: we register the whole system as ONE undo step ourselves.
        CCArray* created = lel->createObjectsFromString(tpll::toLevelString(p), true, false);
        if (!created || created->count() == 0) return "The editor created no objects.";
        lel->addToUndoList(UndoObject::createWithArray(created, UndoCommand::Paste), false);
        saveConfig(p.cfg);
        ok = true;
        auto msg = fmt::format("{} | items {}-{}, groups {}-{}{}.{}", p.summary, p.itemFirst, p.itemLast, p.groupFirst,
            p.groupLast, p.cfg.collision ? fmt::format(", blocks {}-{}", p.blockFirst, p.blockLast) : std::string(),
            note);
        log::info("TpllKit stacks: {} ({} objects)", msg, p.objs.size());
        return msg;
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
    CCLabelBMFont* m_stacksLbl = nullptr;
    CCLabelBMFont* m_clicksLbl = nullptr;
    CCMenuItemToggler* m_omega = nullptr;
    CCMenuItemToggler* m_carry = nullptr;
    CCMenuItemToggler* m_collision = nullptr;
    CCMenuItemToggler* m_extended = nullptr;
    CCMenuItemToggler* m_autoFix = nullptr;
    tpll::Config m_cfg;

    CCPoint toMenu(CCPoint p) {
        return m_buttonMenu->convertToNodeSpace(m_mainLayer->convertToWorldSpace(p));
    }

    bool init() {
        if (!Popup::init(400.f, 306.f)) return false;
        this->setTitle("Pickup Stacks");
        m_cfg = loadConfig();
        auto size = m_mainLayer->getContentSize();

        CCLabelBMFont* lastLabel = nullptr;
        auto addField = [&](char const* label, TextInput*& field, std::string value, float labelX, float fieldX, float y) {
            auto l = CCLabelBMFont::create(label, "bigFont.fnt");
            l->setScale(.33f);
            l->setAnchorPoint({0.f, .5f});
            l->setPosition({labelX, y});
            m_mainLayer->addChild(l);
            lastLabel = l;
            field = TextInput::create(85.f, "0", "bigFont.fnt");
            field->setFilter("0123456789");
            field->setMaxCharCount(10);
            field->setString(value);
            field->setPosition({fieldX, y});
            m_mainLayer->addChild(field);
        };
        float y0 = size.height - 52.f, step = 32.f;
        addField("Stack Count", m_stacksIn, std::to_string(m_cfg.stacks), 15.f, 160.f, y0);
        m_stacksLbl = lastLabel;
        addField("Click Requirement", m_clicksIn, std::to_string(m_cfg.clicks), 15.f, 160.f, y0 - step);
        m_clicksLbl = lastLabel;
        addField("Start Item ID", m_itemIn, std::to_string(m_cfg.itemStart), 15.f, 160.f, y0 - 2 * step);
        addField("Start Group ID", m_groupIn, std::to_string(m_cfg.groupStart), 215.f, 345.f, y0);
        addField("Add Per Click", m_perIn, std::to_string(m_cfg.perClick), 215.f, 345.f, y0 - step);
        addField("Start Block ID", m_blockIn, std::to_string(m_cfg.blockStart), 215.f, 345.f, y0 - 2 * step);

        float ty = y0 - 3 * step + 2.f, ts = 26.f;
        auto addToggle = [&](char const* label, CCMenuItemToggler*& tog, bool value, float labelX, float toggleX, float y,
                             std::function<void(bool)> cb = nullptr) {
            auto l = CCLabelBMFont::create(label, "bigFont.fnt");
            l->setScale(.3f);
            l->setAnchorPoint({0.f, .5f});
            l->setPosition({labelX, y});
            m_mainLayer->addChild(l);
            tog = CCMenuItemExt::createToggler(
                CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
                CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
                [cb](CCMenuItemToggler* t) { if (cb) cb(!t->isToggled()); }
            );
            tog->toggle(value);
            tog->setScale(.6f);
            tog->setPosition(toMenu({toggleX, y}));
            m_buttonMenu->addChild(tog);
        };
        addToggle("Omega (to 2147483647)", m_omega, m_cfg.omega, 15.f, 185.f, ty);
        addToggle("Counter Mode", m_carry, m_cfg.carry, 15.f, 185.f, ty - ts, [this](bool on) { this->refreshLabels(m_extended && m_extended->isToggled(), on); });
        addToggle("Collision Mode", m_collision, m_cfg.collision, 15.f, 185.f, ty - 2 * ts);
        addToggle("Extended Integer (exp.)", m_extended, m_cfg.extended, 215.f, 380.f, ty,
                  [this](bool on) { this->onExtended(on); });
        addToggle("Auto-fix IDs", m_autoFix, m_cfg.autoFix, 215.f, 380.f, ty - ts);
        this->refreshLabels(m_cfg.extended, m_cfg.carry);

        auto addButton = [&](char const* text, float x, std::function<void()> cb) {
            auto btn = CCMenuItemExt::createSpriteExtra(ButtonSprite::create(text), [cb](CCMenuItemSpriteExtra*) { cb(); });
            btn->setScale(.8f);
            btn->setPosition(toMenu({x, 34.f}));
            m_buttonMenu->addChild(btn);
        };
        addButton("Check IDs", 80.f, [this] { this->onCheckIds(); });
        addButton("Create", size.width - 70.f, [this] { this->onCreate(); });
        return true;
    }

    // The field names change with the mode: Stack Count / Counters / Limbs, Click Requirement / Limb Base.
    // (A toggler callback runs before the toggle flips, so the new state is passed in explicitly.)
    void refreshLabels(bool ext, bool carry) {
        m_stacksLbl->setString(ext ? "Limbs" : (carry ? "Counters" : "Stack Count"));
        m_clicksLbl->setString(ext ? "Limb Base" : "Click Requirement");
    }

    // Switching Extended Integer Mode on/off swaps untouched defaults (999,999 / 3 <-> 1e9 / 4 limbs).
    void onExtended(bool on) {
        if (on) {
            if (parse(m_clicksIn, tpll::kNormalCap) == tpll::kNormalCap) m_clicksIn->setString(std::to_string(tpll::kDefaultLimbBase));
            if (parse(m_stacksIn, 3) == 3) m_stacksIn->setString("4");
        } else {
            if (parse(m_clicksIn, 0) == tpll::kDefaultLimbBase) m_clicksIn->setString(std::to_string(tpll::kNormalCap));
            if (parse(m_stacksIn, 0) == 4) m_stacksIn->setString("3");
        }
        this->refreshLabels(on, m_carry->isToggled());
    }

    tpll::Config readFields() {
        tpll::Config c;
        c.omega = m_omega->isToggled();
        c.carry = m_carry->isToggled();
        c.collision = m_collision->isToggled();
        c.extended = m_extended->isToggled();
        c.autoFix = m_autoFix->isToggled();
        c.perClick = parse(m_perIn, 1);
        c.stacks = static_cast<int>(std::clamp<int64_t>(parse(m_stacksIn, 3), 1, 100000));
        c.clicks = parse(m_clicksIn, tpll::kNormalCap);
        c.itemStart = static_cast<int>(std::clamp<int64_t>(parse(m_itemIn, 1), 1, tpll::kMaxID));
        c.groupStart = static_cast<int>(std::clamp<int64_t>(parse(m_groupIn, 1), 1, tpll::kMaxID));
        c.blockStart = static_cast<int>(std::clamp<int64_t>(parse(m_blockIn, 1), 1, tpll::kMaxID));
        return tpll::sanitize(c);
    }

    // "Check IDs": reports conflicts with objects already in the level and moves the start IDs to free ranges.
    void onCheckIds() {
        if (!LevelEditorLayer::get()) return toast("Open the level editor first.", NotificationIcon::Warning);
        auto c = readFields();
        auto used = tpll::scanUsedIds();
        auto conflicts = tpll::findConflicts(c, used);
        if (!conflicts.any()) return toast("No ID conflicts. These IDs are free.", NotificationIcon::Success);
        auto what = tpll::describeConflicts(conflicts);
        if (!tpll::autoFixIds(c, used)) return toast("In use: " + what + ". No free range left.", NotificationIcon::Error, 4.f);
        m_itemIn->setString(std::to_string(c.itemStart));
        m_groupIn->setString(std::to_string(c.groupStart));
        m_blockIn->setString(std::to_string(c.blockStart));
        toast("In use: " + what + ". Moved to free IDs.", NotificationIcon::Info, 4.f);
    }

    void onCreate() {
        bool ok = false;
        auto msg = generate(readFields(), ok);
        toast(msg, ok ? NotificationIcon::Success : NotificationIcon::Warning, ok ? 4.f : 4.5f);
        if (ok) this->onClose(nullptr);
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
        btn->setPosition({win.width * 0.70f, win.height * 0.68f});  // left of the Copy button
        menu->addChild(btn);
        this->addChild(menu, 100);
        return true;
    }
};
