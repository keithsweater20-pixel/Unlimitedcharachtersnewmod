#include "LevelScan.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace tpll {

UsedIds scanUsedIds() {
    UsedIds used;
    auto lel = LevelEditorLayer::get();
    if (!lel || !lel->m_objects) return used;
    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        // groups the object belongs to
        if (obj->m_groups && obj->m_groupCount > 0) {
            short const* data = obj->m_groups->data();
            for (int j = 0; j < obj->m_groupCount; ++j)
                if (data[j] > 0) used.groups.insert(data[j]);
        }
        auto fx = typeinfo_cast<EffectGameObject*>(obj);
        if (!fx) continue;
        // groups a trigger targets
        if (fx->m_targetGroupID > 0) used.groups.insert(fx->m_targetGroupID);
        switch (obj->m_objectID) {
            case 1817:  // Pickup
            case 1611:  // Count
            case 1615:  // Item display
                if (fx->m_itemID > 0) used.items.insert(fx->m_itemID);
                break;
            case 1815:  // Collision trigger: Block A / Block B
                if (fx->m_itemID > 0) used.blocks.insert(fx->m_itemID);
                if (fx->m_itemID2 > 0) used.blocks.insert(fx->m_itemID2);
                break;
            case 1816:  // Collision block
                if (fx->m_itemID > 0) used.blocks.insert(fx->m_itemID);
                break;
            default: break;
        }
    }
    return used;
}

std::string describeConflicts(Conflicts const& c) {
    auto list = [](std::vector<int> const& v) {
        std::string s;
        for (size_t i = 0; i < v.size() && i < 4; ++i) s += (i ? "," : "") + std::to_string(v[i]);
        if (v.size() > 4) s += "...";
        return s;
    };
    std::string out;
    auto add = [&](char const* name, std::vector<int> const& v) {
        if (v.empty()) return;
        if (!out.empty()) out += " | ";
        out += std::string(name) + " " + list(v);
    };
    add("groups", c.groups);
    add("items", c.items);
    add("blocks", c.blocks);
    return out;
}

}  // namespace tpll
