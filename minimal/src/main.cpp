#include <Geode/Geode.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/SetTextPopup.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <atomic>
#include <climits>
#include <string>

using namespace geode::prelude;

namespace {
    // Largest manual limit the settings page accepts. "Unlimited" goes all the way
    // to INT_MAX, which is the biggest length the game's own int field can hold.
    constexpr int64_t kManualCeiling = 1'000'000;

    int configuredLimit() {
        auto mod = Mod::get();
        if (mod->getSettingValue<bool>("unlimited")) return INT_MAX;
        int64_t value = mod->getSettingValue<int64_t>("char-limit");
        value = std::clamp<int64_t>(value, 1, kManualCeiling);
        return static_cast<int>(value);
    }

    // True when the raised limit should apply right now.
    bool limitsActive() {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return false;
        if (mod->getSettingValue<bool>("editor-only") && !LevelEditorLayer::get()) return false;
        return true;
    }

    // Fields that must keep their original limits:
    //  - number fields (IDs, offsets...) so values can never overflow what they parse into
    //  - password fields
    bool mustKeepLimit(CCTextInputNode* node) {
        if (node->m_usePasswordChar) return true;
        // Opt-in: let number boxes be raised too (very large numbers may overflow).
        if (Mod::get()->getSettingValue<bool>("number-fields")) return false;
        if (node->m_numberInput) return true;
        if (static_cast<int>(node->m_valueType) != 0) return true; // Float / Int / Uint
        std::string allowed = node->m_allowedChars;
        if (!allowed.empty() && allowed.find_first_not_of("0123456789.-+") == std::string::npos) {
            return true; // digits-only character filter
        }
        return false;
    }

    // Only the first few raises are logged, so the log proves the hooks run
    // without flooding it on every keystroke.
    void logRaise(char const* where, int from, int to) {
        static std::atomic<int> logged{0};
        int count = logged.fetch_add(1);
        if (count < 12) {
            log::info("TpllKit: raised text limit {} -> {} in {}", from, to, where);
        }
        // One on-screen notice per game session, so you can see the hook fired
        // without reading logs.
        if (count == 0 && Mod::get()->getSettingValue<bool>("show-notice")) {
            std::string text = fmt::format("TpllKit active: limit {} -> {}", from, to);
            Notification::create(text.c_str(), NotificationIcon::Info, 2.f)->show();
        }
    }

    // Diagnostic: the first time each hook runs, show what it saw. This separates
    // "the hook never runs" from "the hook runs but the box's limit is elsewhere".
    void diagnose(char const* where, unsigned bit, int original, bool active, bool keep) {
        static std::atomic<unsigned> seen{0};
        unsigned mask = 1u << bit;
        if (seen.fetch_or(mask) & mask) return;
        log::info("TpllKit hook {} ran: box limit {}, active {}, kept {}", where, original, active, keep);
        if (Mod::get()->getSettingValue<bool>("show-notice")) {
            std::string text = fmt::format("TpllKit {}: box limit {}, on {}, kept {}", where, original, active, keep);
            Notification::create(text.c_str(), NotificationIcon::Info, 2.f)->show();
        }
    }

    // Temporarily raises a text input's own limit for the duration of one call,
    // then puts the original value back. The game's stored limit is never changed
    // permanently, so nothing outside these calls can observe the difference.
    struct LimitScope {
        CCTextInputNode* node;
        int original;
        bool active = false;

        LimitScope(CCTextInputNode* n, char const* where, unsigned bit) : node(n), original(n->m_maxLabelLength) {
            bool on = limitsActive();
            bool keep = mustKeepLimit(n);
            diagnose(where, bit, original, on, keep);
            if (!on) return;
            if (original <= 0) return; // 0 = no explicit limit, leave it alone
            if (keep) return;
            int target = configuredLimit();
            if (original >= target) return;
            node->m_maxLabelLength = target;
            active = true;
            logRaise(where, original, target);
        }

        ~LimitScope() {
            if (active) node->m_maxLabelLength = original;
        }

        LimitScope(LimitScope const&) = delete;
        LimitScope& operator=(LimitScope const&) = delete;
    };
}

// Every typed character goes through onTextFieldInsertText, which is where the game
// compares the current text length against m_maxLabelLength. The other methods are
// covered too, so that if the game also checks the limit while loading or redrawing
// text, an existing long value is not cut short and then saved back truncated.
class $modify(TpllInputNode, CCTextInputNode) {
    bool onTextFieldInsertText(CCTextFieldTTF* sender, char const* text, int len, enumKeyCodes keyCode) {
        LimitScope scope(this, "insert", 0);
        return CCTextInputNode::onTextFieldInsertText(sender, text, len, keyCode);
    }

    void setString(gd::string text) {
        LimitScope scope(this, "setString", 1);
        CCTextInputNode::setString(text);
    }

    void updateLabel(gd::string text) {
        LimitScope scope(this, "updateLabel", 2);
        CCTextInputNode::updateLabel(text);
    }

    void textChanged() {
        LimitScope scope(this, "textChanged", 3);
        CCTextInputNode::textChanged();
    }
};

// SetTextPopup (used for text objects) receives its limit as a parameter and
// hands it to its text input, so the parameter is raised here as well.
class $modify(TpllTextPopup, SetTextPopup) {
    bool init(
        gd::string value, gd::string placeholder, int maxLength,
        gd::string title, gd::string okBtnText, bool showResetBtn, float offset
    ) {
        if (maxLength > 0 && limitsActive()) {
            int target = configuredLimit();
            if (maxLength < target) {
                logRaise("SetTextPopup::init", maxLength, target);
                maxLength = target;
            }
        }
        return SetTextPopup::init(value, placeholder, maxLength, title, okBtnText, showResetBtn, offset);
    }
};

$on_mod(Loaded) {
    log::info("TpllKit loaded: enabled={}, limit={}",
        Mod::get()->getSettingValue<bool>("enabled"), configuredLimit());
}
