# Aimbot (stage D8 - first version that really changes a shot)

**Short version:** the Aimbot now has a real connection to the game's ball. When you let go of a ball, it works out the speed that makes the ball fall into the hoop
you are aiming at, and sets that speed on the ball. If the hoop is farther than your "Max shot distance", it does nothing and your throw stays exactly as you threw it.

**It has never run in the real game.** Everything below was tested on a PC against a *pretend* game that I built from the numbers in your lobby facts file.
The PC tests prove my code does what I meant. They do **not** prove the real game behaves like my pretend game. The list of what is unproven is further down - please read it.
Your first test in the headset is how we find out. Every shot you take writes a report into the facts file, so whatever goes wrong, the next stage can fix it.

## What you asked for, and where it stands
| Your request | State |
|---|---|
| The ball goes in the hoop you aim your shot at, from anywhere | **Built (D8). UNPROVEN in the real game.** |
| A meter/slider that caps how far you can shoot from | **Built and working** (Basketball page, "Max shot distance") |
| Range 5 m to Unlimited, 1 m per step | **Built**: 5, 6, 7 ... 49 m, then 50 = "Unlimited" |
| Farther than the cap = the aimbot does not activate | **Built** and tested (also at exactly the cap: still allowed) |
| Works in the lobby AND in official matches | **Unknown.** I built it the same way for both, but I could not check a match (see "The match file" below). |

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

## The menu page (Menu -> Basketball)
- **Aimbot switch.** Always starts OFF when the game starts (never saved, on purpose).
- **Max shot distance slider.** 5 m ... 49 m, and 50 = "Unlimited". Starts at Unlimited. Your slider position IS saved.
- **Aimbot link card:** a dot (grey = off, orange = looking, green = connected, red = failed), a short line about what the link is doing, and one line about your **last shot**
  ("shot #3: AIMED from 14 m - SCORED, 0.03 m off centre", or "shot #4: not aimed - 31 m is farther than your 20 m limit").
- **Scan ball and hoops** button: the old read-only scan from D7c. The Aimbot does **not** need it.

## The rules (aimbot.cpp - 65 PC checks)
When the ball leaves your hand, the Aimbot looks at 3 things:
1. Is the switch on?
2. Is it a shot? At least 2.5 m/s fast and at least 20 degrees upward. A drop, a bounce or a flat pass is left alone.
3. Which hoop, and is it within your cap?

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

## The match file (what it showed, honestly)
You sent a second facts file and called it "the official match". Its content was **identical to the lobby file** (same class numbers, same hoops, same "not game ball" flags).
I cannot prove from the file that it was recorded in a real official match. It also did not contain any ball control data, because that scan ran out of its line budget.
So **I have no match-specific facts yet.** The first D8 test in a match will give them, because every shot writes a report that includes the game state (`state`, `inCompetition`, `gmMode`, `solo`, `nba`).

## The game's own shot assist (fact from the lobby file)
The game has its own assist (`BasketballShotAssist`: `_maxAssistDistance` 10 m, `_maxAssistAngle` 15 degrees, ...) and the numbers may come from the game's server.
**My Aimbot does not use it, and does not touch it.** If the game re-applies its own speed after ours, the report will say so, and the fallback plan (making the game's own assist stronger instead) is described at the end.

## Stage D8 test checklist (short)
1. Upload the new `payload` folder and the docs to GitHub (Add file -> Upload files, commit). **Do not upload `TZ_KEYSTORE_B64.txt`.**
2. Wait for the green check under **Actions**. (Red = send me a screenshot of the error. No new release is made when the build fails.)
3. In the patcher press **Update**, start the game.
4. Menu -> **Movement**: check Speed / Jump / Gravity still work (nothing there changed).
5. Menu -> **Basketball**: turn **Aimbot ON**, leave the slider at **Unlimited**. Within about 10 seconds the card should say **"Aimbot link: connected"** (green).
   If it stays orange or turns red after 30 seconds: stop, press **Get facts**, send me the file.
6. **Lobby test:** close the menu. Shoot 5-6 times from different places: close, medium, far, and aim badly on purpose (too weak, a bit sideways). After each shot, open the menu and look at the "last shot" line.
7. **Distance test:** set the slider to **10 m**. Shoot once from closer than 10 m (should be aimed) and once from farther than 10 m (should say "not aimed ... farther than your 10 m limit").
8. **Official match test:** join an official match and repeat step 6 with 3-4 shots.
9. Press **Get facts**, send me the file, and tell me for each shot: lobby or match, what you SAW (went in / missed / looked strange / the ball jerked), and what the "last shot" line said.

If the game lags or closes, or the ball does something odd, tell me which step it was. If an aimed ball misses, that is not a failure of the test - the report shows exactly why.

## What the new lines in the facts file mean
- `aim: found the game's classes ...` - the Aimbot found all the fields it needs (or says which one is missing).
- `aim: bound to your ball control object ... by asking the game` - step 1 worked.
- `aim: engine check (first release) ...` - compares the physics body's position with the ball's own position. They must agree.
- `aim: SHOT #n ... decision: ...` - what it saw (position, speed, angle), the game state, the rules' answer, and the speed it worked out, set and read back.
- `aim: SHOT #n result ...` - did the ball come down through the ring height, how far from the centre, `_shotMade`, and whether the game kept our speed.
- `aim: SHOT #n flight samples ...` - the real ball against my prediction at about 15 points. This is how I check the drag and the physics steps.
- `t=... aim: link connected | ... AIMED n (scored n, missed n ...)` - counters. Dribbles and drops are counted but not written one by one.

## If it does not work - what happens next
- **Link never connects:** the card and the facts file give the reason (for example "doorway never called" or "wrong thread").
- **Connected but the ball is not changed:** the decision line says why (too far, no hoop, not a shot, no solution, ...).
- **The game puts its own speed back:** the report says "THE GAME CHANGED OUR SPEED". Next try: make the game's own assist stronger (its distance and angle numbers) instead of setting the speed myself.
- **The ball is aimed but misses by a constant amount:** the flight samples show whether it is drag, gravity or the physics step, and I correct that number.

## Online game - one honest warning
GymClass is an online game. Speed, jump, gravity and especially an aimbot may be visible to other players and could put your account at risk.
Nothing in the files can tell me whether the game detects it. Your call.
