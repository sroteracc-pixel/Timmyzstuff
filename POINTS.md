# Shot points (stage D10)

**Short version:** Menu -> **Troll** tab -> **Shot points**. Turn the switch ON, pick a number with the slider, and every basket **you** score should be worth that many points - in a game and outside a game.

The slider has 12 stops: **1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, then 999**.
The switch starts **OFF** every time the game starts (never saved, on purpose). The slider remembers where you left it.

## Honest status (read this first)
- **Tested only on a PC against a PRETEND game.** It has never run in your real game.
- What the PC tests prove: the menu page and slider work, every stop gives the right number, the mod writes it into the ball and keeps it there, nothing crashes when something is missing, and your other features (movement, Aimbot, Aimbot Bank) behave exactly as before.
- **What I could NOT verify (and cannot from here):** whether the real game's scoring actually *reads* the number I change. Your earlier facts file showed that each basketball has a "point value" (`BasketballProperties._pointValue`) and that shots carry a "point value" too (`ShotData.PointValue`). But I could not see the game's scoring code, so I do not know:
  1. if the score really uses that number at the moment the ball goes in (it might copy it earlier, or count points somewhere else);
  2. if the game rewrites the number back right after the throw (the mod puts it back, but if the game counts the basket in between, the change is too late);
  3. if the **team scoreboard**, your **personal score** and the **popup** all use it, or only some of them.
- So this is a **first attempt**. If the score does not change, the facts file will tell me exactly why (see "What to send me") and the next version can write the score directly instead.

## What it touches
- **Only the ball you are using** (the one you hold or just let go of, and for 12 seconds after). Other players' balls are **never** changed - nobody else's shots are affected.
- Nothing is called in the game. The mod only writes one whole number into the ball's own settings and puts the game's own number back when you turn the switch OFF.
- It works **alone** (Aimbot OFF) and **together** with the Direct Aimbot or the Aimbot Bank.
- **Heads-up:** in a real match the score is shared, so other players will see your higher score on the scoreboard (if it works). Use it where that is OK.

## How to test it (about 5 minutes)
1. Start the game. Open the menu -> **Troll** tab. The "Points link" line should go from "looking..." to **"connected - your ball is kept at N point(s)"** a few seconds after you turn the switch ON. If it says **failed**, send me the facts file.
2. Turn **Shot points** ON. Set the slider to **11**. Wait about 10 seconds.
3. **Outside a game** (lobby / free throw): take a normal shot from behind the 3-point line and score. Watch the score / popup. Does it go up by 11?
4. Change the slider to **1**, then **5**, score again. Does it follow the slider?
5. Set the slider to the last stop (**999**) and score once.
6. **In a game:** repeat with 11 (or any number) from a 2-point spot. Does your team score / your own score go up by that number?
7. Turn the switch OFF and score once: it should be back to the game's normal points.
8. Optional: turn the Direct Aimbot ON and hold Y while shooting - the points should still follow the slider.
9. Press **Basketball -> Scan ball and hoops** once, then **Get facts** in the patcher and send me the file.

## What to send me
The **facts file** after the test. Look for lines starting with `points:` - they say:
- `found where the ball keeps its point value` - the mod found the right place in the game.
- `first look at a ball of yours ... its point value is N` - what the game had in it before I changed it.
- `the game put its own number N into the ball's point value ... I put N back` - the game rewrites it (and how many seconds after you let go).
- `BASKET on ball ...: the ball's point value was N - check the scoreboard: did it go up by N?` - the moment of a basket. **Please tell me if the score did or did not go up by N** (and which kind: team score, your score, popup).
- `switch off ... put back` - the clean exit.

The Scan button (step 9) now also writes the game's **scoring classes** (`ScoreManager`, `PlayerScore`, `ScoreSync`, `PlayerNetworked`, ...) with their fields, methods and the first bytes of each scoring method's code. If the simple version does not work, that is what I need to write the next one.

## Safety
- Nothing is saved except the slider position. The switch is OFF at every start.
- If anything is missing or wrong (a field renamed after a game update, a ball that was destroyed, memory the system refuses to write), the mod stops, says why on the Troll page and in the facts file, and changes nothing else. The game keeps running.
- The updater, the backup and Restore are not touched.
