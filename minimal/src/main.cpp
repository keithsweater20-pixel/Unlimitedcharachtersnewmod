#include <Geode/Geode.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/SetTextPopup.hpp>

#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace {
    // Hard ceiling so a bad setting value can never make the limit absurd.
    constexpr int64_t kHardCeiling = 1'000'000;

    int configuredLimit() {
        int64_t value = Mod::get()->getSettingValue<int64_t>("char-limit");
        value = std::clamp<int64_t>(value, 1, kHardCeiling);
        return static_cast<int>(value);
    }

    // True when the raised limit should apply right now.
    bool limitsActive() {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return false;
        if (mod->getSettingValue<bool>("editor-only") && !LevelEditorLayer::get()) return false;
        return true;
    }

    // Numeric fields (IDs, group numbers, offsets...) keep their original limits,
    // so values can never overflow the integer they are parsed into.
    bool isNumericField(CCTextInputNode* node) {
        if (node->m_numberInput) return true;
        std::string allowed = node->m_allowedChars;
        if (allowed.empty()) return false;
        return allowed.find_first_not_of("0123456789.-+") == std::string::npos;
    }

    // Temporarily raises a text input's own limit for the duration of one call,
    // then puts the original value back. The game's stored limit is never changed
    // permanently, so nothing outside these calls can observe the difference.
    struct LimitScope {
        CCTextInputNode* node;
        int original;
        bool active = false;

        explicit LimitScope(CCTextInputNode* n) : node(n), original(n->m_maxLabelLength) {
            if (!limitsActive()) return;
            if (original <= 0) return; // 0 = no explicit limit, leave it alone
            if (isNumericField(n)) return;
            int target = configuredLimit();
            if (original >= target) return;
            node->m_maxLabelLength = target;
            active = true;
        }

        ~LimitScope() {
            if (active) node->m_maxLabelLength = original;
        }

        LimitScope(LimitScope const&) = delete;
        LimitScope& operator=(LimitScope const&) = delete;
    };
}

// Every typed character goes through this method, which is where the game
// compares the current text length against m_maxLabelLength.
class $modify(TpllInputNode, CCTextInputNode) {
    bool onTextFieldInsertText(CCTextFieldTTF* sender, char const* text, int len, enumKeyCodes keyCode) {
        LimitScope scope(this);
        return CCTextInputNode::onTextFieldInsertText(sender, text, len, keyCode);
    }

    // Also covered so that loading an existing long value into a field is not
    // cut short by the original limit and then saved back truncated.
    void setString(gd::string text) {
        LimitScope scope(this);
        CCTextInputNode::setString(text);
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
            maxLength = std::max(maxLength, configuredLimit());
        }
        return SetTextPopup::init(value, placeholder, maxLength, title, okBtnText, showResetBtn, offset);
    }
};

$on_mod(Loaded) {
    log::info("TpllKit loaded, character limit set to {}", configuredLimit());
}
