# TpllKit

Two separate features in one iOS Geode mod: a text-limit remover and a Pickup Stack generator for the level editor.

A Geode mod for Geometry Dash (iOS) that raises the artificial character limit on free-text inputs while the level editor is open (for example the text-object popup).

- Mod ID: `keithsweater20.tpllkit`
- Geode: 5.11.2
- Geometry Dash: 2.2081 (iOS)

## How it works

Geometry Dash text boxes (`CCTextInputNode`) compare their length against a per-box limit (`m_maxLabelLength`). TpllKit:

1. Hooks `CCTextInputNode::onTextFieldInsertText` and `CCTextInputNode::setString` and raises that limit for the duration of the call only.
2. Hooks `SetTextPopup::init` and raises the `maxLength` argument it receives.

Numeric fields (IDs, offsets) are left alone, and by default nothing changes outside the level editor.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| Enabled | on | Master switch |
| Character limit | 50000 | New maximum (1 - 1,000,000) |
| Editor only | on | Only apply inside the level editor |

## Build

Pushing to GitHub runs `.github/workflows/build.yml`, which builds the iOS `.geode` file and uploads it as the `TpllKit-iOS` artifact.

## Known limits

- This only removes limits enforced by the client text input. Server-side limits (comments, level names, descriptions) are unaffected.
- Very long text in a text object can make the editor slow.

## Pickup Stack generator (v1.8.0)

The **Stacks** button in the level editor opens a menu. **Create** builds the whole trigger system in the middle of the screen and adds it as **one undo step** (the editor's Undo removes all of it).

| Option | What it does |
|---|---|
| Stack Count | Number of stacks. Each fills to the Click Requirement, resets, and hands over to the next. |
| Click Requirement | Clicks per stack (up to 999,999, or 2,147,483,647 with Omega). |
| Omega | Allows the 32-bit maximum, 2,147,483,647. |
| Counter Mode | Counters fill to the cap one after another (Counter 1 takes the clicks). |
| Add Per Click | How much counter 1 gains per click (Counter / Extended mode, up to 100). |
| Collision Mode | Clicks come from a Collision trigger and Collision blocks instead of a Touch trigger. |
| Start Item / Group / Block ID | First ID of each range the system uses. |
| **Auto-fix IDs** | Before creating, the level is scanned. If any group, item or block ID in the range is already used, the system moves to the nearest free range instead of tangling with your objects. |
| **Check IDs** | Shows what is in use and fills in free start IDs. |
| **Extended Integer (exp.)** | See below. |

Every generated system gets a text object with its item, group and block ID ranges, "Stack N" / "Counter N" / "Limb N" labels, and live Item Displays. Before anything is created the plan is validated: every Toggle / Count / Touch target group must have an owner, every spawned trigger must be reachable, and every watched item must be written. If that check fails nothing is created.

### Experimental: Extended Integer Mode

Item values stop at 2,147,483,647, so this mode does not raise that limit. It stores one big number across several items ("limbs") in base B (default 1,000,000,000). Limb 1 takes the clicks. Each limb has a Count trigger for the value B; when it fires, that limb resets to 0 and the next limb gets +1, so carries cascade through every limb. With 4 limbs of base 1e9 it counts to 10^36 - 1.

- **Limbs** = the Stack Count box, **Limb Base** = the Click Requirement box (2 to 2,147,483,647).
- When the top limb would overflow, every limb is set to its maximum (B-1) and the click source switches off. It never wraps to zero.
- Limbs are shown most significant first. The game's Item Display does not pad lower limbs with leading zeros, so 1,000,000,005 appears as limbs `1` and `5`, not `1` and `000000005`.
- Turn the toggle off to get the normal modes back unchanged.

## Tests

`tests/sim_test.cpp` runs the generated trigger plans in a small model of Pickup / Count / Toggle / Touch behaviour and checks them against reference models (stack cycling, Counter Mode, Omega, multi-limb carries and saturation, ID allocation, validation, level strings):

```
g++ -std=c++20 -O1 -o sim_test tests/sim_test.cpp minimal/src/stacks/Plan.cpp && ./sim_test
```

## Honest limits

- The model assumes triggers behave as described above (a toggled-off group stops its remaining triggers; clicks are decided at the start of a click). Nothing has been run inside the real game yet, so if the game orders trigger work differently the first run may differ.
- With Add Per Click above 1, the very last click of Counter Mode may show one count above the cap if the game runs sibling Pickup triggers after the stop toggle.
- The ID scan reads Pickup, Count, Item Display and Collision objects; IDs used by other item triggers are not scanned.
