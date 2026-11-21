// No Icon Animations
// Per game mode, stops the icon animation. Everything here is visual: the game's own
// physics, hitboxes and rotation-based gameplay (rotated gravity) are left alone.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCAnimatedSprite.hpp>

using namespace geode::prelude;

namespace {
    // Which setting applies to this player's current game mode.
    char const* modeKey(PlayerObject* p) {
        if (p->m_isSwing) return "swing";
        if (p->m_isDart) return "wave";
        if (p->m_isBird) return "ufo";
        if (p->m_isShip) return "ship";
        if (p->m_isBall) return "ball";
        if (p->m_isRobot) return "robot";
        if (p->m_isSpider) return "spider";
        return "cube";
    }

    bool disabledFor(PlayerObject* p) {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return false;
        return mod->getSettingValue<bool>(modeKey(p));
    }

    bool inLevel() {
        return PlayLayer::get() || LevelEditorLayer::get();
    }
}

class $modify(NoAnimPlayer, PlayerObject) {
    // Every rotation of the icon (jump spin, tilt, wave angle, ...) ends up here, including
    // the ones produced by the game's rotate actions. Rotated-gravity sections
    // (m_isSideways) are skipped so the icon still faces the way the gameplay turns.
    void setRotation(float rotation) {
        if (!m_isSideways && disabledFor(this)) rotation = 0.f;
        PlayerObject::setRotation(rotation);
    }

    // The cube / ball spin actions are not started at all for a disabled mode.
    void runRotateAction(bool ground, int type) {
        if (disabledFor(this)) return;
        PlayerObject::runRotateAction(ground, type);
    }

    void runBallRotation(float speed) {
        if (disabledFor(this)) return;
        PlayerObject::runBallRotation(speed);
    }

    void runBallRotation2() {
        if (disabledFor(this)) return;
        PlayerObject::runBallRotation2();
    }

    // Safety net: some icon parts are rotated directly instead of through the player.
    void update(float dt) {
        PlayerObject::update(dt);
        if (m_isSideways || !disabledFor(this)) return;
        if (m_isRobot || m_isSpider) return;
        for (CCNode* n : {static_cast<CCNode*>(m_iconSprite), static_cast<CCNode*>(m_iconSpriteSecondary),
                          static_cast<CCNode*>(m_iconSpriteWhitener), static_cast<CCNode*>(m_vehicleSprite),
                          static_cast<CCNode*>(m_vehicleSpriteSecondary), static_cast<CCNode*>(m_vehicleSpriteWhitener),
                          static_cast<CCNode*>(m_birdVehicle)}) {
            if (n && n->getRotation() != 0.f) n->setRotation(0.f);
        }
    }
};

// Robot and spider animate through frame animations (running, jumping...). While one of them is
// switched off, new animations are not started, so the sprite stays on its current pose.
class $modify(NoAnimSprite, CCAnimatedSprite) {
    bool blocked() {
        if (!inLevel()) return false;
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return false;
        if (typeinfo_cast<GJSpiderSprite*>(this)) return mod->getSettingValue<bool>("spider");
        if (typeinfo_cast<GJRobotSprite*>(this)) return mod->getSettingValue<bool>("robot");
        return false;
    }

    void runAnimation(gd::string animation) {
        if (blocked()) return;
        CCAnimatedSprite::runAnimation(animation);
    }

    void runAnimationForced(gd::string animation) {
        if (blocked()) return;
        CCAnimatedSprite::runAnimationForced(animation);
    }

    void tweenToAnimation(gd::string animation, float duration) {
        if (blocked()) return;
        CCAnimatedSprite::tweenToAnimation(animation, duration);
    }
};
