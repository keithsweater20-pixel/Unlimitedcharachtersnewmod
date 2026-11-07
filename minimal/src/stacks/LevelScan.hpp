#pragma once
#include "Plan.hpp"

namespace tpll {
// Group, item and block IDs that objects in the open level already use.
UsedIds scanUsedIds();
// "groups 3,4 | items 2" style text for a conflict report (first few IDs only).
std::string describeConflicts(Conflicts const& c);
}  // namespace tpll
