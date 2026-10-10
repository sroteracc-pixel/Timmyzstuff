# Aimbot (stage D7c) - what exists, what does not

**Short version:** the menu page and the "should the aimbot act?" rules and aim maths are built and tested on a PC.
**The part that moves the real ball is NOT built yet.** Nobody (me included) has seen how this game throws the ball, so I did not
guess. Stage D7c adds a scan that collects exactly that, once in the lobby and once in an official match. Send me both facts files and stage D8 builds the real connection.

In the headset today, the Aimbot switch only changes what the menu says and what the facts file logs. **It does not change any shot.**
The menu says so ("Game link: not built yet").

## What you asked for, and where it stands
| Your request | State |
|---|---|
| Aimbot: the ball goes in the hoop you aim your shot at, from anywhere | Rules + aim maths built and tested on a PC. **Real ball connection: NOT built (D8).** |
| A meter/slider that caps how far you can shoot from | **Built** (Basketball page, "Max shot distance") |
| Range 5 m to Unlimited, 1 m per step | **Built**: 5, 6, 7 ... 49 m, then 50 = "Unlimited" |
| Farther than the cap = the aimbot does not activate | **Built in the rules** and tested (see below). It only has an effect once D8 exists. |

## The menu page (Menu -> Basketball)
- **Aimbot switch.** Always starts OFF when the game starts (it is never saved, on purpose).
- **Max shot distance slider.** 5 m ... 49 m, and 50 = "Unlimited". Starts at Unlimited. Your slider position IS saved.
- **Scan ball and hoops** button (collects facts for D8; see below).

## The rules (aimbot.cpp - tested on a PC, 51 checks)
When the ball leaves your hand, the aimbot looks at 3 things:
1. Is the switch on?
2. Is it a shot? (fast enough and thrown upward enough - a drop, a bounce or a flat pass is left alone)
3. Which hoop are you aiming at, and is it within your cap?

How "which hoop" works: the hoop that is closest to the direction you threw (looked at from above). If no hoop is within 40 degrees of
your throw direction, the aimbot does nothing.

The distance cap:
- Distance = along the floor, from the ball to the middle of that hoop (not up/down).
- Farther than your cap -> **the aimbot does not activate** and the throw is exactly what you threw.
- Exactly at the cap -> still allowed.
- Slider on **Unlimited (50)** -> the distance check is skipped completely (shoot from anywhere).
- Tested with 20,000 random shots: Unlimited never refuses because of distance, and raising the cap never makes a shot that worked stop working.

The aim maths: given the ball position and the hoop position, it finds the launch speed that makes the ball fall into the ring
(the ball must come down steeply enough, at least 38 degrees, to fit through). It keeps your own arc if that works, otherwise it makes the arc higher.
Tested against an independent step-by-step flight simulation: the ball ends within about 0.00001 m of the ring.

## ASSUMPTIONS (guesses that the real game can prove wrong)
These are numbers I picked, not facts from the game:
- A "shot" = at least 2.5 m/s fast and at least 20 degrees upward.
- Hoop must be within 40 degrees of your throw direction.
- Launch angle between 35 and 80 degrees, entry angle at least 38 degrees, never faster than 60 m/s.
- The ball falls with the game's real gravity, flies with **no air drag, no spin**, and the rim does not bounce it out. **If the real game has
  drag, a different ball gravity or a bouncy rim, the ball can miss.** D8 must measure these.
- The ball gravity is NOT the player gravity you already saw in Movement (-0.9). The ball's own gravity is unknown.

## Lobby vs official match (why your other aimbot may only work in the lobby)
You told me the Astryx aimbot works in the multiplayer lobby but not in an official match. I can NOT tell you why, and I can NOT promise mine will work in both.
(Sorry: in my first version I called the lobby "single player". That was my wrong guess from class names. The lobby is a multiplayer room.)

**What your lobby facts file (stage D7b) showed - these are facts from the game:**
- The game's script thread is the one called `UnityMain`. The controller function is called on it. That is where we can safely touch the ball later.
- Even in the lobby you are connected to a network room (Normcore, `Normal.Realtime`).
- The ball is the class `ShovelTools.Basketball` (101 fields). Its network copy is `BasketballStateSync`, which holds: held by left/right hand, who holds it, who owned it last,
  a text called `_shotData`, `_isGameBall`, `_isInPlay`, and a ball action state. It also has `IsCurrentOwner`.
- The hand-grab library (Autohand) holds the ball's physics body (`rb`), `beingHeld`, `_throwing`, and has `OnRelease` / `GetVelocity` methods.
- The game has its OWN shot assist: classes `BasketballShotAssist`, `ShotAssistParams`, `PredictedShotResult`, `BankShotCandidate`, `ThrowAssist`, `BasketballAssist`. Only their names are known so far, not what they do.
- The Unity functions for reading and setting a ball's speed exist (found in `libunity.so`).
- `BallPhysics` is NOT the basketball (it is a pitch / flight-path system), so I stopped using it.
- The rim: `GymClassRimBend._rim` is a Grabbable (you can grab the rim), `_isNorth` says which end. Rim bounciness seen: 0.33, friction 0.7.

**What I only GUESS (not proven):**
- The ball has an `_isGameBall` flag, and there are names like `IsGameManagedBall`, `BallControlManager`, `GameManager`. So official-match balls may be handled differently from lobby balls
  (for example owned or controlled by the game). That could be why changing the ball works in the lobby and not in a match. NOT proven.
- Other possible reasons: the match uses the assist code with fixed settings, or the game checks the shot data.

**What D7c does about it:** it scans exactly these classes (their fields and methods) and their running copies, in the lobby and in a match, so the two files can be compared:
the ball, its sync, the game's shot assist and its settings (real numbers), the hoops, and the game / match state (`GameManager`, `BasketballGameContext`).

**Two ways D8 could work (I will choose from the facts, no promise):**
1. Calculate the shot myself and set the ball's speed after you let go (what this page promises). Needs the ball to be yours to change.
2. Make the game's OWN shot assist stronger (change its settings). The game then does the aiming itself, which may work in matches too. Only possible if the assist settings can be changed and are not forced by the match.
Either way D8 will check EVERY shot: it reads the ball again a few frames later and writes into the facts file whether the change stuck, and whether you were in the lobby or a match.

## What the new scan collects ("Scan ball and hoops", stage D7c)
Read-only. It writes into the facts file:
1. About 25 exact classes (fields with positions, methods): the ball `Basketball`, the game's shot assist (`BasketballShotAssist`, `ShotAssistParams`, `PredictedShotResult`, `BankShotCandidate`, `RimTarget`, `ShotData`, `ThrowAssist`, `BasketballAssist`),
   the hoops (`BasketballGoal`, `BasketballGoalManager`, `HoopManager`, `NetRimReference`), the ball control (`BallControl`, `BallControlManager`, ...), the state (`GameManager`, `BasketballGameContext`)
   and two network classes (`RealtimeView`, `RealtimeTransform`: who owns an object). Names that are not found are listed in one line.
   (The long class index, the assembly list and the classes I already have are left out, so the file stays small.)
2. Which Unity functions exist for reading and setting a ball's speed (it only LOOKS them up, it **calls nothing**).
3. **25 seconds after you press the button** (so you can close the menu, pick up a ball, take a shot and hold a ball), it reads the running objects: the balls (held or not, last shot data),
   the sync objects, the hoops, the assist settings (real numbers), the ball control, and the game / match state.
4. Which kinds of threads the game has.

## Online game - one honest warning
GymClass is an online game. Speed, jump, gravity and especially an aimbot may be visible to other players and could put your account at risk.
Nothing in the files can tell me whether the game detects it. Your call.

## Stage D7c test checklist (in the headset)
1. Update the patcher, start the game. In the menu go to **Basketball**: the **Aimbot** card and the **Game link: not built yet** card are there. The switch does nothing in the game yet.
2. Check the **Movement** page still works (Speed, Jump, Gravity). Nothing there changed.
3. **Scan 1 - multiplayer lobby:** start the game fresh (so the facts file is small), join the lobby, spawn a ball and stand near a hoop.
   Open the menu -> Basketball and press **Scan ball and hoops**. Then:
   - close the menu (B),
   - pick up a ball, take ONE shot at a hoop,
   - pick a ball up again and HOLD it until the menu says "Scan done" (about one minute in total).
4. Open the patcher, press **Get facts**, send me the file. Call it "lobby".
5. **Scan 2 - official match:** close the game completely and open it again (a fresh facts file). Join an official match and wait until the game ball is on the court.
   Do exactly the same scan (press Scan, close the menu, take ONE shot, hold a ball). Open the menu with the same button combination as before.
6. Press **Get facts** and send me that file too. Call it "official match".

If the game lags a lot or closes during a scan, tell me which step it was. (I could only test the scan on a PC. How a live official match reacts to it is unknown.)

## Stage D8 plan (needs your new facts file)
Using the scan data: detect the moment the ball is released, read its position and velocity, read each hoop's position,
call the rules + maths above, write the new velocity on the game's main thread, and read it back a few frames later to check that the ball kept it
(written to the facts file for every shot, with "lobby" or "official match"). Still unknown until then: how the game releases the ball,
whether the game (or the network system) re-applies its own velocity afterwards, who owns the ball in an online game, the ball's real gravity / drag,
and whether it works in BOTH the lobby and official matches.
