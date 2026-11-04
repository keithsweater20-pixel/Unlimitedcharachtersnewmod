# TpllKit

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
