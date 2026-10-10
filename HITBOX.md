# Hitbox expander + See hitbox (stage D11)

**Short version:** Menu -> **Troll** tab. There are two new things:
- **Hitbox expander** - a switch and a slider. The slider goes from **1.0x** (normal hitbox) to **5.0x** (5 times as big), in steps of 0.1. When the switch is ON, the hitboxes of **your two hands** are made that many times bigger, so steals and blocks should get easier.
- **See hitbox** - a separate switch. It asks the game to show its own "hand collider" picture on your hands, so you can see what is going on.

Both switches start **OFF** every time the game starts (never saved, on purpose). The slider remembers where you left it (it starts at 2.0x the first time).

## Honest status (read this first)
- **Tested only on a PC against a PRETEND game.** It has never run in your real game.
- What the PC tests prove: the menu works, the slider gives exactly 1.0x ... 5.0x, the mod finds your two hands, makes their hitboxes exactly that many times bigger **starting from the game's own size** (so moving the slider never piles up), puts the game's own sizes back when you switch off, ignores other players' hands, copes with the game putting sizes back, new hands, destroyed hitboxes and engine errors, and never disturbs your Aimbot or Shot points.
- **What I could NOT verify (and cannot from here) - this is the important part:**
  1. **Which hitboxes the game really uses for steals and blocks.** The game's files show that each hand has a list of hitboxes (`_handColliders`), and the ball's class (`Basketball`) has functions named `CheckHandCollision` and `TryKnockLooseFromBotHold` (the names suggest hand touches and knocking a ball out of a bot's hold). But I have **not seen what those functions do**. The steal / block might use a different hitbox, a separate "trigger", or a distance check instead. If so, bigger hand hitboxes will change nothing for steals, and only the facts file (below) can tell me what to change.
  2. **Whether other players feel it.** In an online game the steal or knock-out might be decided on the *other* player's side. Then your bigger hitboxes would not matter to them. I can't know this without a test.
  3. **Whether the engine calls I use exist in your game.** I use the engine's normal "set size" calls. If the game removed them, the mod falls back to the engine's internal ones; if both are missing it says so on the page and does nothing.
  4. **Whether big hitboxes make your hands jumpy.** Very big hitboxes can push the ball, make it stick to your hand, or make your hand physics shake. **Start at 1.5x - 2x** and go up slowly.
  5. **What "See hitbox" really draws.** It uses the game's own display function (`SetHandColliderVisual`) with the model your hand already has. It might show a ghost hand, an outline, or nothing at all (a final game build may have it switched off inside). The function takes a number as its second value and I do not know what that number means to the game; I pass `1.0` for "show" and `0.0` for "hide" (the facts file says which kind of value it wants). If the picture looks wrong, switch See hitbox OFF. It also might not grow with the slider. The mod hides and shows it again once after you stop moving the slider, to give it a chance to follow.

## What it touches
- **Only the hitboxes in the lists of your two hands** (the two hands that belong to your own ball control). **Other players' hands are never touched.**
- Hitboxes of kinds that can't be resized (mesh) are only counted, not changed.
- Everything is **put back to the game's own size** when you switch the expander OFF, move the slider back to 1.0x, or the game replaces your hands. If the game changes a hitbox itself, the mod notices (it looks every 2 seconds) and sets it again, and writes a line about it.
- A hitbox that would end up bigger than 50 m is left alone.
- The mod only talks to the game when something changed (at most 10 looks a second). If the engine refuses 12 calls in a row it stops by itself and the page says why.
- It works **alone** and **together** with the Aimbot, the Aimbot Bank and Shot points.
- **Heads-up:** this changes your own hands in a game other people are in. Use it where that is OK.

## How to test it (about 10 minutes)
1. Start the game, open the menu -> **Troll** tab. Turn **Hitbox expander** ON (2.0x). Under the slider a line should go from "looking for your hands..." to something like **"connected: 2 hands, 8 hitboxes made 2.0x bigger"** within a few seconds. If it says **FAILED**, press **Get facts** in the patcher and send me the file.
2. Turn **See hitbox** ON. Look at your hands. **Tell me what you see:** nothing / a see-through shape / an outline / a ghost hand / something else - on one hand or both? Does it get bigger when you move the slider?
3. With a ball and another player (or a bot) near you: try to **steal**. Does it feel easier at 2.0x than with the switch OFF? Try a **block** too.
4. Move the slider to **5.0x**. Same test. Watch for: shaky hands, the ball sticking to your hand or flying off, dribbling feeling strange, being pushed, anything weird.
5. Ask the other player (if you can): do they see anything different? Do your steals work **on them** the same as on a bot?
6. Turn the **Hitbox expander** OFF: everything should feel normal again right away. (See hitbox can stay on or off, it is separate.)
7. Optional: turn the Aimbot or Shot points on at the same time. They should behave as before.
8. Press **Basketball -> Scan ball and hoops** once, then **Get facts** in the patcher and send me the file.

## What to send me
The **facts file** after the test, plus a few words on what you saw and felt (steals easier? blocks easier? did others notice? any shaking?). In the facts file look for lines starting with `hitbox:` - they say:
- `found where your hands keep their hitboxes ...` and which engine calls exist ("game call", "engine call" or "NO").
- `your hands have N hitboxes (left hand a, right hand b): M can be resized ...` followed by the **names and sizes of every hitbox**. This is how I learn which hitbox is which (palm, fingers, ...).
- `set N hitboxes of your hands to 2.0x their own size` - the mod did its job.
- `the game changed N of your hitboxes since I set them ...` - the game puts its own sizes back (and how often).
- `asked the game to show its hand collider display ...` - See hitbox was asked (what it draws I can only learn from you).
- the final `hitbox: link ...` summary with all counters.

The Scan button (step 8) now also writes the game's **hand classes** (`Hand`, `BallControl`) and the first bytes of every method with `Collider`, `HandCollision` or `KnockLoose` in its name. If the simple version does not help your steals, that is what I need to find the code that really decides a steal and write the next version.

## Safety
- Nothing is saved except the slider position. Both switches are OFF at every start.
- If anything is missing or wrong (a field renamed after a game update, a hand that was destroyed, an engine call that fails), the mod stops, says why on the Troll page and in the facts file, and changes nothing else. The game keeps running.
- The updater, the backup and Restore are not touched.

## Parked
**Shot points** (the points slider) is still on the Troll page but did not work in the last test (the game rewrites its own number). I have kept it for later, as you asked; the scan data it needs is already collected.
