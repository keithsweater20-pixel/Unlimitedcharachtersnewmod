#pragma once
// Pure logic for the Pickup Stack generator: configuration, ID allocation, the trigger plan
// and its validation. Nothing in here includes Geode or Cocos, so it can be tested off-device
// (see tests/sim_test.cpp).
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace tpll {

constexpr int64_t kNormalCap = 999'999;
constexpr int64_t kOmegaCap = 2'147'483'647;  // INT_MAX, the biggest value an item can hold
constexpr int kMaxStacks = 200;
constexpr int kMaxLimbs = 60;
constexpr int kMaxID = 9999;
constexpr int64_t kDefaultLimbBase = 1'000'000'000;

struct Config {
    int stacks = 3;               // stacks, counters, or (Extended mode) limbs
    int64_t clicks = 999'999;     // click requirement; in Extended mode: the limb base
    bool omega = false;           // clicks go up to 2,147,483,647
    int itemStart = 1;
    int groupStart = 1;
    bool carry = false;           // Counter Mode: counters fill to the cap one after another
    int64_t perClick = 1;         // amount added to the first stack per click (Counter / Extended)
    bool collision = false;       // clicks come from a Collision trigger + Collision blocks
    int blockStart = 1;
    bool extended = false;        // Experimental: Extended Integer Mode (multi-limb counter)
    bool autoFix = true;          // pick a free ID range automatically when IDs are already used
};

enum class Kind { Touch, CollisionTrigger, CollisionBlock, Block, Pickup, Count, Toggle, Display, Text };

struct Obj {
    Kind kind = Kind::Block;
    float x = 0.f, y = 0.f;
    int item = 0;          // Pickup / Count / Display
    int64_t count = 0;     // Pickup amount, Count target value
    bool override_ = false;  // Pickup: set the item to `count` instead of adding
    int target = 0;        // group a Touch/Collision/Count spawns, or a Toggle changes
    bool on = false;       // Toggle: switch the target on or off
    int blockId = 0;       // CollisionTrigger (Block A) / CollisionBlock
    int group = 0;         // the group this object belongs to (0 = none)
    bool spawn = false;    // Pickup/Toggle: runs when its group is spawned (not by position)
    std::string text;      // Text
};

struct Plan {
    Config cfg;                    // the configuration actually used (after clamping / ID fixing)
    std::vector<Obj> objs;
    std::string error;             // non-empty: nothing should be created
    std::string summary;           // one line describing the system
    int groupFirst = 0, groupLast = 0, itemFirst = 0, itemLast = 0, blockFirst = 0, blockLast = 0;
};

struct UsedIds {
    std::set<int> groups, items, blocks;
};

struct Needs {
    int groups = 0, items = 0, blocks = 0;
};

struct Conflicts {
    std::vector<int> groups, items, blocks;
    bool any() const { return !groups.empty() || !items.empty() || !blocks.empty(); }
};

int64_t capFor(Config const& c);          // cap used by Normal / Counter mode
Config sanitize(Config c);                // clamp every field to a sane range
Needs needsFor(Config const& c);          // how many IDs of each kind the config uses
Conflicts findConflicts(Config const& c, UsedIds const& used);
// Moves the start IDs to the nearest free ranges. False if no free range exists.
bool autoFixIds(Config& c, UsedIds const& used);

// Builds the whole trigger system around (bx, by). Never touches the game.
Plan build(Config c, float bx, float by);

// Every problem in a plan: Toggle/Count/Touch targets nothing owns, spawn-triggered objects
// nobody spawns, watched items nobody writes. Empty = a fully connected system.
std::vector<std::string> validate(Plan const& p);

// Standard Geometry Dash object string ("1,ID,2,X,3,Y,...;") for a plan.
std::string toLevelString(Plan const& p);

// Saved-per-level text form of a Config.
std::string encodeConfig(Config const& c);
Config decodeConfig(std::string const& s);

}  // namespace tpll
