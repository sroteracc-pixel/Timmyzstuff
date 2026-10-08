# The in-game menu (ready for later)

Lives in `menu/`. It is part of the PAYLOAD (it runs inside the game), not the patcher app.

| File | What it does | Tested? |
|---|---|---|
| `menu_input.*` | Opens on **both triggers held + A click**, closes on **B** or the **X** button | Yes, on a PC (incl. the 3-second safety hold when Menu Enabled is off) |
| `theme.*` | Dark navy by default, 5 accent swatches (cyan, purple, magenta, blue, white) | Yes, on a PC |
| `settings.*` | The Settings switches + menu distance, saved with the colours in one file | Yes, on a PC |
| `menu_ui.*` | The window: title, **X** button, Main and Settings tabs, colour pickers | Not compiled yet (needs Dear ImGui) |
| `tz_menu.*` | Glues the above together: `init()` once, `frame()` every frame | Not compiled yet |
| `integration.h` / `integration_stubs.cpp` | The 3 things only your game setup can answer (see below) | Placeholders |

## The 3 placeholders
1. **Read the controllers** - triggers, A, B.
2. **Block game input while the menu is open** - so clicks don't hit the game behind it.
3. **A folder to save colours in.**

Until these are filled in, the menu stays closed (it never pretends to work).

## Test the logic on a PC
    cd menu
    g++ -std=c++17 -Isrc tests/test_menu.cpp src/menu_input.cpp src/theme.cpp src/settings.cpp -o /tmp/t && /tmp/t

## Layout (from your screenshot)
Sidebar: Settings, Favorites, Active, Movement, Basketball, Baseball, Soccer, Football, Paintball, Boxing.
Only Settings has content (Menu Enabled, Show Menu Button, Enable Notifications, Smooth UI, Menu Distance, Accent Color).
The other tabs say "Nothing here yet." Icons from the screenshot need an icon font or images - not added yet.
