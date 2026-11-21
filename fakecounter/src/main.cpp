// Fake FPS & CPS Counter
// Draws two on-screen counters whose numbers come ONLY from the mod settings.
// Nothing here reads the real frame rate or real clicks, and nothing hooks the game's
// timing or input, so the real FPS and real clicks are never touched.
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

using namespace geode::prelude;

namespace {
    // Trim spaces on both ends.
    std::string trim(std::string s) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    // True when the whole text is a number (so wobble can be applied to it).
    bool parseNumber(std::string const& text, double& out) {
        std::string s = trim(text);
        if (s.empty()) return false;
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
        out = v;
        return true;
    }

    struct Row {
        CCSprite* icon = nullptr;
        CCLabelBMFont* label = nullptr;
        std::string imagePath;   // the path the current icon was made from
        std::string shown;       // the text currently shown (without prefix)
    };
}

class FakeCounterNode : public CCNode {
protected:
    Row m_fps;
    Row m_cps;
    std::mt19937 m_rng{std::random_device{}()};
    float m_timer = 0.f;
    std::string m_lastKey; // snapshot of the settings, to react instantly to changes

    bool init() {
        if (!CCNode::init()) return false;
        this->setID("fake-counter"_spr);
        this->setZOrder(100000);

        m_fps.label = CCLabelBMFont::create("", "bigFont.fnt");
        m_cps.label = CCLabelBMFont::create("", "bigFont.fnt");
        for (auto* l : {m_fps.label, m_cps.label}) {
            l->setAnchorPoint({0.f, 0.5f});
            this->addChild(l);
        }
        return true;
    }

    // Value shown for one counter: a number (+ random wobble) or the text verbatim.
    std::string valueFor(std::string const& setting, int64_t wobble, double maxValue) {
        double base;
        if (!parseNumber(setting, base)) return trim(setting).empty() ? std::string("0") : setting;
        base = std::clamp(base, 0.0, maxValue);
        if (wobble > 0) {
            std::uniform_real_distribution<double> d(-static_cast<double>(wobble), static_cast<double>(wobble));
            base += d(m_rng);
        }
        base = std::clamp(base, 0.0, maxValue);
        return std::to_string(static_cast<long long>(std::llround(base)));
    }

    void refreshIcon(Row& row, std::filesystem::path const& path) {
        std::string p = path.string();
        if (p == row.imagePath) return;
        row.imagePath = p;
        if (row.icon) {
            row.icon->removeFromParent();
            row.icon = nullptr;
        }
        if (p.empty()) return;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) return;
        auto spr = CCSprite::create(p.c_str());
        if (!spr) {
            log::warn("Fake counter: could not load image {}", p);
            return;
        }
        spr->setAnchorPoint({0.f, 0.5f});
        this->addChild(spr);
        row.icon = spr;
    }

public:
    // Called every frame by the driver below. Reads the settings each time, so changes apply instantly.
    void tick(float dt) {
        auto mod = Mod::get();
        bool master = mod->getSettingValue<bool>("show-counters");
        bool showFps = master && mod->getSettingValue<bool>("show-fps");
        bool showCps = master && mod->getSettingValue<bool>("show-cps");
        std::string fakeFps = mod->getSettingValue<std::string>("fake-fps");
        std::string fakeCps = mod->getSettingValue<std::string>("fake-cps");
        int64_t fpsWobble = mod->getSettingValue<int64_t>("fps-wobble");
        int64_t cpsWobble = mod->getSettingValue<int64_t>("cps-wobble");
        double px = mod->getSettingValue<double>("pos-x");
        double py = mod->getSettingValue<double>("pos-y");
        double scale = mod->getSettingValue<double>("scale");
        int64_t opacityPct = mod->getSettingValue<int64_t>("opacity");
        auto fpsImg = mod->getSettingValue<std::filesystem::path>("fps-image");
        auto cpsImg = mod->getSettingValue<std::filesystem::path>("cps-image");

        // Any change to the settings refreshes the numbers right away.
        std::string key = fmt::format("{}|{}|{}|{}|{}|{}|{}", fakeFps, fakeCps, fpsWobble, cpsWobble,
            fpsImg.string(), cpsImg.string(), showFps * 2 + showCps);
        bool changed = key != m_lastKey;
        m_lastKey = key;

        m_timer += dt;
        // Real counters tick a few times per second; the wobble re-rolls at the same pace.
        if (changed || m_timer >= 0.25f) {
            m_timer = 0.f;
            m_fps.shown = valueFor(fakeFps, fpsWobble, 100000.0);
            m_cps.shown = valueFor(fakeCps, cpsWobble, 1000.0);
        }
        refreshIcon(m_fps, fpsImg);
        refreshIcon(m_cps, cpsImg);

        // Without a picture, the row says "FPS"/"CPS" in text so it is never just a bare number.
        m_fps.label->setString((m_fps.icon ? m_fps.shown : "FPS " + m_fps.shown).c_str());
        m_cps.label->setString((m_cps.icon ? m_cps.shown : "CPS " + m_cps.shown).c_str());

        auto win = CCDirector::get()->getWinSize();
        this->setPosition({static_cast<float>(px / 100.0 * win.width), static_cast<float>(py / 100.0 * win.height)});

        GLubyte opacity = static_cast<GLubyte>(std::clamp<int64_t>(opacityPct, 0, 100) * 255 / 100);
        float labelScale = 0.45f * static_cast<float>(scale);

        float y = 0.f;
        Row* rows[2] = {&m_fps, &m_cps};
        bool visible[2] = {showFps, showCps};
        for (int i = 0; i < 2; ++i) {
            Row& row = *rows[i];
            row.label->setVisible(visible[i]);
            if (row.icon) row.icon->setVisible(visible[i]);
            if (!visible[i]) continue;

            row.label->setScale(labelScale);
            row.label->setOpacity(opacity);
            float rowH = row.label->getContentSize().height * labelScale;
            float x = 0.f;
            if (row.icon) {
                float ch = row.icon->getContentSize().height;
                float s = ch > 0.f ? rowH / ch : 1.f;
                row.icon->setScale(s);
                row.icon->setOpacity(opacity);
                row.icon->setPosition({0.f, y - rowH / 2});
                x = row.icon->getContentSize().width * s + 4.f * static_cast<float>(scale);
            }
            row.label->setPosition({x, y - rowH / 2});
            y -= rowH * 1.25f;
        }
    }

    static FakeCounterNode* create() {
        auto ret = new FakeCounterNode();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// Keeps the counters on top of whatever scene is running: every frame it moves the node into the
// current scene if it is not already there, so the counters show in every scene (menus, levels,
// the editor, transitions).
class FakeCounterDriver : public CCObject {
public:
    Ref<FakeCounterNode> node;

    void update(float dt) override {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene || !node) return;
        if (node->getParent() != scene) {
            node->removeFromParentAndCleanup(false);
            scene->addChild(node, 100000);
        }
        node->tick(dt);
    }
};

// Started once, when the main menu first appears (the game's textures are ready by then).
class $modify(FakeCounterMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        static bool started = false;
        if (!started) {
            if (auto node = FakeCounterNode::create()) {
                started = true;
                auto driver = new FakeCounterDriver();
                driver->node = node;
                CCDirector::get()->getScheduler()->scheduleUpdateForTarget(driver, 0, false);
                // intentionally never released: the driver lives as long as the game
            }
        }
        return true;
    }
};
