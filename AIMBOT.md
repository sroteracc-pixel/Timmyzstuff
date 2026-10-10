# Aimbot (stage D8c - it works; now it only activates while you hold Y)

**Short version:** the Aimbot has a real connection to the game's ball. When you let go of a ball **while holding Y**, it works out the speed that makes the ball fall into the hoop
you are aiming at, and sets that speed on the ball. If you are not holding Y, or the hoop is farther than your "Max shot distance", it does nothing and your throw stays exactly as you threw it.

**Stage D8 was tested by you in the headset: it works.** Stage D8b tried "only when jumping" - you told me it did not work. Stage D8c **removes that jump rule** and replaces it with what you asked for:
the **"Hold Y to aim"** switch (Basketball page, ON by default). With it ON the Aimbot only changes a throw when the **Y button (left controller) is held when you let go of the ball**.
It is a button I can read directly from the controller, so it does not depend on my guesses about the game's jump code.

**The Y part has never run in the real game.** It was tested on a PC against a *pretend* game and a pretend controller. See "Hold Y to aim" below for what is verified and what is a guess.
Every shot still writes a report into the facts file (now including whether Y was held), so whatever goes wrong, the next stage can fix it.

## What you asked for, and where it stands
| Your request | State |
|---|---|
| The ball goes in the hoop you aim your shot at, from anywhere | **Built (D8). UNPROVEN in the real game.** |
| A meter/slider that caps how far you can shoot from | **Built and working** (Basketball page, "Max shot distance") |
| Range 5 m to Unlimited, 1 m per step | **Built**: 5, 6, 7 ... 49 m, then 50 = "Unlimited" |
| Farther than the cap = the aimbot does not activate | **Built** and tested (also at exactly the cap: still allowed) |
| Works in the lobby AND in official matches | **Lobby: you said it works.** Official match: still unknown (see "The match file" below). |
| Only activate if the player jumps | **Tried in D8b - you said it did not work. Removed in D8c** (the game's jump state is still written into the report, but it decides nothing). |
| Activate on Y being held | **Built (D8c), NOT yet tested in the headset.** Switch "Hold Y to aim" on the Basketball page, ON by default. |

## What the Aimbot does, step by step (when the switch is ON)
1. **Finding your ball.** The game keeps one "ball control" object for you (`BallControlManager`). The Aimbot asks the game for it
   (`GameManager.get_Instance().GetOwnedBallControlManager()`). If that does not work it searches the game's memory for the `GameManager` and reads your ball control from it.
   The card on the Basketball page shows a green dot and "Aimbot link: connected" when this worked.
2. **Noticing that you let go.** About 185 times a second (inside the controller function that the game's own thread calls) it reads your ball control.
   It sees a release in up to three independent ways: a "seconds since release" timer jumping back to 0, the "time of last release" changing, or the ball's own `_unheldTime` starting to count.
3. **Waiting 0.04 seconds** so the game has applied your throw, then it reads the ball's position and speed from the ball's physics body.
4. **Deciding** (the rules, see below): is it a shot, which hoop, is that hoop within your cap?
5. **Working out the new speed.** It reads the game's real gravity, the ball's real air drag and the real physics step from the game, simulates the flight step by step, and finds the launch speed that
   makes the ball come down through the middle of the ring. It keeps your own arc if that works, otherwise it makes the arc higher.
6. **Setting it once**, then reading the speed back to check that the ball took it.
7. **Watching the flight** and writing ONE report per shot into the facts file: what it saw, what it did, where the ball really went, and what the game's own `_shotMade` says.

If anything looks wrong in steps 1-5 (a number that makes no sense, a hoop that is not found, the ball still held, a game call that errors), it does **nothing** and your throw is untouched.
Three game-call errors in a row stop the whole Aimbot until you switch it off and on again.

## Hold Y to aim (stage D8c)
**What it does.** At the moment you let go of the ball, the Aimbot checks the **Y button on your left controller**.
- Y held -> the Aimbot works as before (shot check, hoop choice, distance cap, new speed).
- Y not held -> your throw is left alone, exactly as you threw it. The menu says "not aimed - hold Y to aim".
- Two small allowances so you do not have to be exact: Y also counts if you let go of Y **up to 0.1 second before** you let go of the ball, or press Y a moment **after** (about 0.04 second, before the Aimbot decides).
- Switch **OFF** = Y is not needed, every shot is aimed (like D8).

**How Y is read.** Two readers look at the real controller: the menu's own thread (about 50 times a second) and the controller function that the game calls (about 185 times a second, before the menu can hide any button).
If **either** one sees Y held, Y counts as held. Y is button number 0x200 in the controller's button number (Meta's standard list: A = 1, B = 2, X = 0x100, Y = 0x200, left-stick click = 0x400; your stage B file contains exactly these numbers).
The facts file counts how often each reader saw Y held (`Y button reads: menu thread N (held in N), doorway N (held in N)`), so if Y does nothing we can see which reader failed.

**What I checked (verified):**
- Your stage B file shows the controller's button number changing to `0x200` (6 times) next to `0x100` (X), `0x400` (left stick click), `0x1`/`0x2` (A/B), `0x100000` (menu button). That is the same layout as Meta's public button list, where `0x200` = Y on the left controller.
- The menu already reads the real buttons this way (it opens with both triggers + A), so the reading itself works.
- The PC tests (with a pretend controller and pretend game) show: Y held -> aimed and scored; no Y -> left alone and the ball flies as thrown; Y only after the shot -> left alone; either reader alone is enough; Y never readable -> left alone and says so.

**What I could NOT check (guesses):**
1. That `0x200` is the button you call "Y" on your Quest 3S controller (it is on Meta's standard layout; I cannot see your hands).
2. **What the game itself does with Y.** If Y already does something in GymClass (jump? menu? emote?), holding it while you shoot will do that too. I do **not** hide Y from the game. Tell me if that is a problem and I can hide it while the Aimbot is ON.
3. That the controller function the game calls includes the left controller's buttons. (That is why there are two readers; the menu's own reader is the proven one.)
4. The timing: the 0.1 s / 0.04 s allowances are my guess. If you let go of Y early and the shot is not aimed, the report says how long before the release Y was last held.

**What happens when it cannot read Y** (no reading for a whole second): with "Hold Y to aim" ON the throw is **left alone** and the menu says "cannot read the Y button". It never guesses.
Turn the switch off and the Aimbot works for every shot.

**The jump state is still written into every shot report** (`jump: vertical state ...`), but it no longer decides anything. If your jump button is also Y, the report's "jump button left pressed" tells me.

**The switch is saved** (it starts ON the first time; if you turn it off it stays off next time). The Aimbot's own on/off switch is still never saved.

## The menu page (Menu -> Basketball)
- **Aimbot switch.** Always starts OFF when the game starts (never saved, on purpose).
- **Max shot distance slider.** 5 m ... 49 m, and 50 = "Unlimited". Starts at Unlimited. Your slider position IS saved.
- **Hold Y to aim switch** (D8c). ON = aim only while Y (left controller) is held as you let go. OFF = aim every shot.
- **Aimbot link card:** a dot (grey = off, orange = looking, green = connected, red = failed), a short line about what the link is doing, and one line about your **last shot**
  ("shot #3: AIMED from 14 m - SCORED, 0.03 m off centre", "shot #4: not aimed - 31 m is farther than your 20 m limit", or "shot #5: not aimed - hold Y to aim").
- **Scan ball and hoops** button: the old read-only scan from D7c. The Aimbot does **not** need it.

## The rules (aimbot.cpp - 65 PC checks)
When the ball leaves your hand, the Aimbot looks at 3 things:
1. Is the switch on?
2. Is it a shot? At least 2.5 m/s fast and at least 20 degrees upward. A drop, a bounce or a flat pass is left alone.
3. Which hoop, and is it within your cap?
4. (D8c) If "Hold Y to aim" is ON: was Y held when you let go?

"Which hoop": the hoop closest to the direction you threw (seen from above). If no hoop is within 40 degrees of your throw direction, nothing happens.
If two hoops are equally good, the nearer one wins.

The cap: distance = along the floor, from the ball to the middle of that hoop. Farther than the cap -> not aimed. Exactly at the cap -> allowed. Slider on Unlimited (50) -> no distance check at all.

The maths: the launch angle stays between 35 and 80 degrees, the ball must come down at least 45 degrees steeply at the ring (so it fits through), and a launch faster than 60 m/s is refused.
With air drag (0.11 in your lobby file) the PC tests put the ball within about 3 cm of the ring centre.

## What is VERIFIED and what is NOT

**Verified (from your lobby facts file - these are real numbers the game gave me):**
- The class and field names and positions that the Aimbot reads: ball `ShovelTools.Basketball` (`_rigidbody`, hoop positions, `_unheldTime`, `_wasShot`, `_shotMade`), ball control `ShovelTools.BallControlManager` (two release timers, `lastReleaseTime`, `_basketball`), `GameManager`.
- The hoops: north (0, 3.1, 12.66), south (0, 3.1, -12.66), hoop radius 0.2286 m. Ball drag 0.11, angular drag 0.1.
- In your lobby sample, 0.35 s after a release the ball control already pointed at the ball, and the "since release" timers read 0.33 and 0.35 s.
- The game's own thread (`UnityMain`) calls the controller function about 185 times a second. That is where the Aimbot works.

**NOT verified - my guesses. Any of these can make the first test fail:**
1. That the release timers / `_unheldTime` really behave as I read them (a timer jumping back to 0 at the moment you let go).
2. That calling the engine's functions (`Rigidbody.get_velocity` / `set_velocity` / `get_position`) from the controller function works without a crash.
3. That the game does **not** put its own speed back on the ball after we set ours (its own shot assist, `ProcessBasketballFlyTo`, or the network). The report says "THE GAME CHANGED OUR SPEED" if it does.
4. That the game's physics works like standard Unity (gravity, then drag, then move, in fixed steps). Each report compares the real flight with my prediction, step by step, so we will see.
5. That the hoop positions stored inside the ball are the real ring centres.
6. That `GetOwnedBallControlManager()` gives YOUR ball control (and not someone else's).
7. **Official matches.** The balls there may be different ("game balls"), and the match may re-apply speeds. Unknown.
8. A very high arc may hit a ceiling. A long pass towards a hoop (not only a shot) may also be guided, because I cannot tell a hard pass from a shot.
9. Whether the online game notices a changed ball speed. See the warning at the end.
10. (D8c) The Y button guesses listed under "Hold Y to aim".

## The match file (what it showed, honestly)
You sent a second facts file and called it "the official match". Its content was **identical to the lobby file** (same class numbers, same hoops, same "not game ball" flags).
I cannot prove from the file that it was recorded in a real official match. It also did not contain any ball control data, because that scan ran out of its line budget.
So **I have no match-specific facts yet.** The first D8 test in a match will give them, because every shot writes a report that includes the game state (`state`, `inCompetition`, `gmMode`, `solo`, `nba`).

## The game's own shot assist (fact from the lobby file)
The game has its own assist (`BasketballShotAssist`: `_maxAssistDistance` 10 m, `_maxAssistAngle` 15 degrees, ...) and the numbers may come from the game's server.
**My Aimbot does not use it, and does not touch it.** If the game re-applies its own speed after ours, the report will say so, and the fallback plan (making the game's own assist stronger instead) is described at the end.

## Stage D8c test checklist (short)
1. Upload the new `payload` folder and the docs to GitHub (Add file -> Upload files, commit). **Do not upload `TZ_KEYSTORE_B64.txt`.**
2. Wait for the green check under **Actions**. (Red = send me a screenshot of the error. No new release is made when the build fails.)
3. In the patcher press **Update**, start the game.
4. Menu -> **Basketball**: turn **Aimbot ON**. **Hold Y to aim** is already ON. Wait for the card to say **"Aimbot link: connected"** (green).
5. **No-Y test:** shoot 3 times WITHOUT touching Y. Each throw should be YOUR throw (not aimed). The last-shot line should say **"not aimed - hold Y to aim"**.
6. **Y test:** hold **Y** (left controller) and shoot 4-5 times, from different spots. Keep Y held until the ball has left your hand. These should be **aimed**.
7. **Tap test:** shoot, and let go of Y a split second before you let go of the ball. It should still be aimed (0.1 s allowance).
8. Turn **Hold Y to aim** OFF: a shot without Y should be aimed again.
9. Does holding Y make the **game** do something by itself (jump, open a menu, ...)? Tell me.
10. Check **Movement** still works.
11. Press **Get facts**, send me the file, and tell me for each shot: Y held or not, what you SAW, and what the "last shot" line said.

**The most important thing to tell me:** if a shot WITH Y held says "not aimed - hold Y to aim" or "cannot read the Y button". The facts file then shows which reader saw Y (the `Y button reads` numbers) and I fix it.

## What the new lines in the facts file mean
- `aim: found the game's classes ...` - the Aimbot found all the fields it needs (or says which one is missing).
- `aim: jump state can be read ...` / `jump state NOT readable: ...` - report only since D8c (the Aimbot does not use it).
- `aim: bound to your ball control object ... by asking the game` - step 1 worked.
- `aim: engine check (first release) ...` - compares the physics body's position with the ball's own position. They must agree.
- `aim: SHOT #n ... decision: ...` - what it saw (position, speed, angle), the game state, **whether Y was held** (`Y button: HELD when you let go` / `NOT held ...`), the jump state at the release (report only), whether "hold Y to aim" was on, the rules' answer, and the speed it worked out, set and read back.
- `aim: SHOT #n result ...` - did the ball come down through the ring height, how far from the centre, `_shotMade`, and whether the game kept our speed.
- `aim: SHOT #n flight samples ...` - the real ball against my prediction at about 15 points. This is how I check the drag and the physics steps.
- `t=... aim: link connected | ... AIMED n (scored n, missed n ...)` - counters, including `Y not held`, `Y unreadable` and the `Y button reads` numbers. Dribbles and drops are counted but not written one by one.

## If it does not work - what happens next
- **Link never connects:** the card and the facts file give the reason (for example "doorway never called" or "wrong thread").
- **Connected but the ball is not changed:** the decision line says why (too far, no hoop, not a shot, no solution, ...).
- **The game puts its own speed back:** the report says "THE GAME CHANGED OUR SPEED". Next try: make the game's own assist stronger (its distance and angle numbers) instead of setting the speed myself.
- **The ball is aimed but misses by a constant amount:** the flight samples show whether it is drag, gravity or the physics step, and I correct that number.

## Online game - one honest warning
GymClass is an online game. Speed, jump, gravity and especially an aimbot may be visible to other players and could put your account at risk.
Nothing in the files can tell me whether the game detects it. Your call.
