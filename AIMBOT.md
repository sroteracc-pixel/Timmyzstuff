# Aimbot + Aimbot Bank (stage D9b)

**Stage D9b - what changed after your first test (the "I cannot measure the backboard" message).**
You sent the facts file. It said: `could not find the backboard's collider: no collider of the board's size near it (4 objects tested, 0 with a collider)`.
That means: the mod needs the backboard's **solid part** (the engine's "collider") to read how bouncy the board is, and it looked for it only on the hoop's own small group of objects (3-4 objects).
In your real game the solid part is somewhere else. My pretend game had put it inside that group, so my tests could not show this. (The numbers for the board itself - size 1.825 x 1.25 m, about 0.4 m behind the ring - were fine.)
The same file also showed that your game has `GetComponent` but **not** `GetComponentInChildren` / `GetComponentInParent`, and no `Physics.get_bounceThreshold` (the mod then assumes the engine's usual 2 m/s and says so).

**What D9b does about it** (in this order, nearest first; it stops at the first collider whose box has the board's size and place):
1. looks at the goal's objects **and up to 4 parent objects above them** (everything below each parent), using the engine call `GetComponentsInChildren` if your game has it, otherwise walking the objects one by one;
2. asks the physics engine what is at the board's place (`Physics.OverlapSphere`, or `OverlapBox` if that is what the game has) - this finds the collider wherever it sits in the scene;
3. as a last resort, looks at every collider of the scene (`Object.FindObjectsOfType`).
It skips triggers and switched-off colliders, never uses a huge collider (the court's floor) as the board, and checks that its own `GetComponent` call really works.
If it **still** cannot find it, the menu says "Bank unavailable - I cannot measure the backboard (could not find the backboard's collider)" and the facts file now tells the **whole story**: what the engine offers, which objects (with their names) were searched, how many colliders were seen, and the nearest colliders with their names, boxes and distances. That is enough for me to fix it in one more step.
A board that fails is searched again after 5 s, 15 s, 45 s, then every 2 minutes (not on every shot).

**Tested only against my pretend game** (now built like your real one: the goal is a small leaf object, the board's solid part sits on a sibling object or far away under the court). All 299 Bank checks pass: it finds the board in 12 variants (with and without each engine call), both hoops, and every bank shot scored. **It has still never run in your real game.**

**Stage D9 adds a second switch, "Aimbot Bank"** (Menu -> Basketball). The first Aimbot (direct shots) is **unchanged**. Read the "Aimbot Bank (stage D9)" part right below first,
then the older parts (D8c) for the direct Aimbot.

## Aimbot Bank (stage D9)

**Short version:** with **Aimbot Bank** ON and **Y held** when you let go of the ball, the mod works out a throw that hits the **front of the backboard**, bounces off it, and **drops into the hoop**.
If a bank shot is **not possible** from where you stand, it does **not** shoot a direct shot for you. Your throw stays exactly as you threw it, and the Basketball page shows **"Bank unavailable - <reason>"** in amber.

**Honest status: the whole thing was tested only on a PC against a PRETEND game. It has never run in your real game.** Everything below says which numbers come from the real game (the mod reads them while you play) and which are untested guesses.

### The rules of the two switches
- **Only one Aimbot mode at a time.** Turning Aimbot Bank ON turns the direct Aimbot OFF, and the other way round. They can also both be OFF. (Tested: 300 clicks on the two switches in random order - they were never ON together.)
- Both start **OFF** every time the game starts (never saved, on purpose, same as before).
- **Max shot distance** and **Hold Y to aim** work for **both** modes. (For Bank, the distance is measured to the hoop, the same as for the direct Aimbot.)
- Dribbles, drops and flat passes are ignored in Bank mode too (not shots, no calculation, no "Bank unavailable").

### Nothing is guessed - the mod reads these from the game at the moment of the shot
The mod measures all of this in the real game while you play, and writes it into the facts file (`bank inputs` line) so I can check it:
| What | Where it comes from |
|---|---|
| Backboard position, direction it faces, width and height | the game's `BasketballGoal` object (`_backboardCenter`, `_backboardNormal`, `_backboardSize`, `_backboardOffset`) and the board's own collider box (found as described in "Stage D9b" above). The mod only trusts the collider if its size and place match the board; if the collider found is not the board's size it is used only for the bounce and the facts file says so. |
| Ring centre and ring radius | the game's `_rimCenter` and `_rimRadius` (your lobby file: radius 0.2286 m, hoops at (0, 3.1, +-12.66)) |
| Ball size | the radius of the ball's own collider times its scale |
| Gravity, air drag, physics step | read from the engine (as in D8) |
| Bounciness and friction of the ball AND the board | the physics materials of the ball and the board, **mixed the way the engine mixes them** (average / minimum / multiply / maximum, found by name at run time). If the board has no material, the engine's default (0.6 / 0.6) is used and the facts file says so. |
| Ball spin | read from the ball's body (or assumed zero if it cannot be read - the file says "assumed") |
| The game's own number for the bounce | `BasketballProperties` runtime values, written as a cross-check (`bank cross-check`) |

### How the bank shot is chosen
1. It tries about 150 flight times. For each one it works out where on the board the ball must touch, so that after the bounce (bounciness, friction and spin included) it falls through the middle of the ring.
2. It simulates every candidate **step by step the way the engine moves the ball** (gravity, drag, move), including the ball being wider than its centre (it checks that the ball's whole surface stays clear of the rim until the ball is below the ring).
3. It throws a candidate away if any of these is true (each has a plain-words reason, shown in the menu and the file):
   - the ball would not hit the **front** of the board, or would hit within 8 cm of the board's edge, or would reach the ring without touching the board at all;
   - it would hit the board gently (below 1.2 m/s or below 1.15 times the engine's bounce threshold) so it might not bounce;
   - it needs a launch faster than 26 m/s, a launch angle outside 25-85 degrees, or a lob more than 4.5 m above the ring;
   - it comes down into the ring flatter than 40 degrees, or touches the rim (needs 1.2 cm spare);
   - you are standing so far sideways that the ball would meet the board at more than 70 degrees sideways;
   - it only works if the bounce is exactly right: it is run 8 more times with a slightly different bounce/friction, and at least 60 % of them must still score.
4. Of the candidates that survive it picks the best-scoring one: most spare room at the rim, holds up best when the bounce is a little different, steeper entry, lower speed, lower arc, and a small preference for a launch angle like the one you threw. It then sets the ball's speed (once, then reads it back to check) - same way as the direct Aimbot.
5. The whole calculation takes about 2-6 ms on a PC (the headset is slower). It has a time limit (6 ms once something good is found, 36 ms at most); if it runs out with nothing solid, it says "ran out of time working it out (try again)" - that is different from "impossible". (When my computer was very busy, one test hit this limit once, so on a slow headset it can happen. If you see it often, tell me.)

### When it shows "Bank unavailable"
The reason is written next to it. Examples:
- **too close to the backboard** (less than about 0.9 m from the board's front)
- **you are behind the backboard**
- **angle too sideways to reach the board**
- **no safe bank shot from this spot** (with the main blocker, for example "the ball would reach the ring without touching the board", "the lob would go too high", "it needs a launch faster than the limit", "it is too sensitive to small differences in the bounce")
- **ran out of time working it out (try again)** (the calculation hit its time limit; the throw is left alone)
- **I cannot measure the backboard / I cannot find that hoop's goal / the game's backboard code is not ready yet** (the mod could not read the real game's data - then it never guesses)
In all of these the throw is **left exactly as you threw it**. There is no direct-shot fallback. (In the PC tests, a far shot of about 12.8 m was "unavailable" and its throw stayed untouched.)

**Where you see it.** In the **menu** (Basketball page, amber line: "shot #7: Bank unavailable - too close to the backboard") and in the **facts file**.
I did **not** add a floating message in the game view: the overlay only exists while the menu is open, and a floating panel would block the game's input. If you want that, tell me and we decide how.

### What was VERIFIED (on the PC, against a pretend game with my own independent physics)
- The two switches: only one ON at a time, in every order, with fast taps, with Y; the direct Aimbot is unchanged (all old tests still pass: 13 older test files).
- The bank maths: **324 checks**, including 300 random scenes (bounce 0.35-0.9, friction 0-0.7, ball radius 0.10-0.15 m, spin, physics step 1/60-1/90 s, from the wing to 1.5 m from the baseline): 232 plans found, 68 refusals, **no plan came within 18.8 mm of the rim**, solve time 1.7 ms on average (worst 6 ms).
- The game-reading part (`aim_bank.cpp`, **299 checks**, 12 of them new in D9b for the real-game layout) against a pretend `libil2cpp`: it finds the goal, the board, the materials; bank shots from most spots 3-11 m in front of **both** hoops hit the board once, did not touch the pole or the rim, and scored; every "unavailable" case leaves the throw alone; the test mode says WOULD BANK and changes nothing.
- Memory and thread checks (AddressSanitizer / ThreadSanitizer) on the new code.

### What I could NOT verify (guesses - any can make the first real test fail)
1. **The real backboard.** I read it from the game's fields and its collider. The numbers in your file look right (1.825 x 1.25 m, 0.4 m behind the ring, normal along the court's length), but **D9b's search for the solid part has never run in the real game**. The first lines of the facts file say what it found (`aim: BANK: measured the backboard ... found via ...`) or the whole story if it did not.
2. **The real bounce.** Whether the real engine mixes the materials like I do, and whether the game changes the ball's speed itself when it hits the board (some games add their own "bank assist" - the report prints `_lastBankAssistInTime` and `_assistInGoal` before and after, and "THE GAME CHANGED OUR SPEED" if it overrides us).
3. **Spin.** Real throws have backspin. If the mod cannot read it, it assumes none and the file says "(assumed)". Spin changes the bounce, so shots may land a little off.
4. **The size of the real error.** The pretend game uses my own physics, so it agrees with my maths by construction. The real game may bounce differently, and then the ball goes in a different spot. That is why each bank shot writes `MEASURED BOUNCE` (speed into the board vs out, versus what the plan used) and flight samples around the touch - they let me correct the numbers.
5. **The rim.** I use the ring radius from the game and treat the rim wire as very thin, keeping 1.2 cm spare. If the real rim is thicker, close shots may clip it.
6. **Network.** Online, the other players' game may not see the bounce the same way. Same warning as at the end of this file.
7. **Y button, ceiling, official matches:** same open points as for the direct Aimbot below.

### Bank test plan - try it from several positions (the hoop you throw at is the one you aim at)
First, **wait about 10 seconds after turning Aimbot Bank on** (the mod measures both backboards in the background). The facts file's `aim: BANK: measured the backboard of the goal with its ring at ...` lines (one per hoop) say it worked.
Do these with **Aimbot Bank ON, Hold Y to aim ON**, and hold **Y** while you let go. After each shot read the amber/white "last shot" line on the Basketball page.
1. **Straight in front, about 6 m from the board** (middle of the court, facing the hoop). Expected: "BANK from ~6 m - hits the board ~0 m off centre". The ball should touch the front of the board once, then drop in.
2. **Left wing, about 45 degrees, 6-8 m.** Expected: bank shot, touching the board on the side you are on.
3. **Right wing, about 45 degrees, 6-8 m.** The same, mirrored.
4. **Farther back, 9-11 m, straight on.** Expected: a higher arc, still a bank. If it says "Bank unavailable", write down the reason - that is useful data.
5. **Near the baseline, to the side of the board, about 11 m from the hoop.** Expected: probably "Bank unavailable - angle too sideways ..." and your throw untouched. Both answers are fine, please tell me what you got.
6. **Very close, under 1 m from the board.** Expected: "Bank unavailable - too close to the backboard".
7. **Far, 12 m or more.** Expected: "Bank unavailable" (needs too much speed or lob). Your throw must stay your throw.
8. **Behind the board** (behind the hoop). Expected: "Bank unavailable - you are behind the backboard".
9. **Y not held:** shoot once or twice without Y. Expected: "not aimed - hold Y to aim", your own throw.
10. **Switching:** turn Aimbot Bank ON -> the direct Aimbot switch must turn OFF. Turn the direct Aimbot ON -> Bank turns OFF. Then shoot a direct shot: it must work as before (no board touch needed).
11. **Distance limit:** set Max shot distance to 8 m and shoot a bank shot from 12 m: it must be "not aimed" (farther than your limit).
12. **Movement** still works (speed, jump, gravity).
13. Press **Get facts** and send me the file, plus for each shot: where you stood (rough), what you SAW (touched the board? where did it go?), and what the last-shot line said.

**What I need back:** the facts file lines starting `aim: BANK:` (what the game offers, and whether the backboard was measured), `aim: SHOT #n bank inputs`, `bank cross-check`, `bank check` (it has `MEASURED BOUNCE`) and `decision: BANK SHOT`. **If the menu still says "I cannot measure the backboard", just send the facts file again - it now contains the reason in full.** The summary line ends with `BANK shots N (scored N, missed N), Bank unavailable N`.
If a bank shot goes wrong, **tell me the position and what the ball did** (missed the board? hit it too low/high? bounced too far? hit the rim?). The measured bounce and flight samples tell me which number to fix.

---

# Aimbot (direct shots) - stage D8c: it works; it only activates while you hold Y

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
