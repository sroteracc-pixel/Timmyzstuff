# Aimbot (stage D7b) - what exists, what does not

**Short version:** the menu page and the "should the aimbot act?" rules and aim maths are built and tested on a PC.
**The part that moves the real ball is NOT built yet.** Nobody (me included) has seen how this game throws the ball, so I did not
guess. Stage D7b adds a scan that collects exactly that, once in practice and once in an online game. Send me both facts files and stage D8 builds the real connection.

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

## Inside and outside online games (why your other aimbot may only work in practice)
You told me the Astryx aimbot works when shooting outside a game but not inside a game. I can NOT tell you why, and I can NOT promise mine will
work in both. Here is what the game files you already sent show (class names only, nothing about how they behave):
- The game has **separate practice code**: `SpawnBasketballSinglePlayer`, `ShovelTools.BasketballSinglePlayer`.
- The game has an **online network system** (Normcore, the file `Normal.Realtime.dll`), and many classes that keep players in sync (`PlayerStateSync`, `StevePlayerSync` ...).
- So in an online game the ball and players are probably "network objects". With this kind of system one player **owns** an object, and a change made by
  someone who does not own it can be ignored or overwritten. That is **my guess** for why changing the ball works in practice and not in a match. It is NOT proven.
Other possible reasons (also unproven): the match uses a different ball class, or the game checks shots itself.

What I did about it, so we find out for real instead of guessing:
- The scan now also writes the network classes (`Realtime`, `RealtimeView`, `RealtimeTransform`: who owns an object, and are you in a room) and looks for the running
  copies of the room and the views. **The facts file from a scan in practice and a scan in an online match will show the difference.**
- Stage D8 will check **every shot**: after it changes the ball, it reads the ball again a few frames later and writes into the facts file whether the ball
  KEPT the new speed (and whether you were in practice or in a match). If a match overwrites it, the facts file will say so and I can try another way (for example asking for ownership of the ball). No promise it works.
- To make sure it works in both places, the only real proof is testing in both. That is why the checklist below has you scan in both.

## What the new scan collects ("Scan ball and hoops")
Read-only, like the Movement scan. It writes into the facts file:
1. The game's ball / hoop / rim / shoot / throw classes, written out with all fields and methods: for example `BallPhysics`, `RimPhysics`,
   `BasketballPlayer`, `BasketballSinglePlayer`, plus the Autohand (VR grabbing) classes. (It leaves out the 12 empty "Parameter..." event
   classes, which only wasted places. I checked this against the real class names from your files.)
2. The network classes (`Normal.Realtime.Realtime`, `RealtimeView`, `RealtimeTransform`) and live copies of the room and the views.
3. Which Unity functions exist for reading and setting a Rigidbody's velocity and a Transform's position (it only LOOKS them up, it **calls nothing**).
4. Live values of the rims, the ball physics (drag, mass, held flag), the player's ball/hand fields and any ball objects it finds.
5. Which thread calls the controller function, listed in `threads:` lines. (Unity objects may only be touched from the game's main thread, so D8 needs to know it.)

The scan is limited in size so the facts file is not cut off. If a network class has a different name in your game, the file says "NOT found in this game".

## Online game - one honest warning
GymClass is an online game. Speed, jump, gravity and especially an aimbot may be visible to other players and could put your account at risk.
Nothing in the files can tell me whether the game detects it. Your call.

## Stage D7b test checklist (in the headset)
1. Update the patcher, start the game. In the menu go to **Basketball**: you see the **Aimbot** card and the **Game link: not built yet** card.
2. Turn the **Aimbot** switch on and off: it should change. Nothing in the game changes (that is correct for D7b).
3. Move **Max shot distance**: 5 m, 6 m ... 49 m, then **Unlimited** at the far end. Close and open the menu: the slider keeps its place, the switch is off again after restarting the game.
4. Check the **Movement** page still works (Speed, Jump, Gravity). Nothing there should have changed.
5. **Scan 1 - practice (outside a game):** stand on a court with a ball and a hoop. Press **Scan ball and hoops**. Wait for "Scan done" (about a minute).
   While it runs, close the menu and pick up a ball and throw a few shots (the menu blocks the game's buttons while it is open).
6. Open the patcher, press **Get facts**, send me the file. Call it "practice".
7. **Scan 2 - online game:** close the game completely and open it again (this starts a fresh facts file). Join an online game, wait until you are on the court with the ball.
   Open the menu -> Basketball, press **Scan ball and hoops**, close the menu, throw a few shots while it runs.
8. When it says "Scan done", press **Get facts** and send me that file too. Call it "online game".

If the game lags a lot or closes during a scan, tell me which step it was. (I could only test the scan on a PC; the lag while scanning in a live online game is unknown.)

## Stage D8 plan (needs your new facts file)
Using the scan data: detect the moment the ball is released, read its position and velocity, read each hoop's position,
call the rules + maths above, write the new velocity on the game's main thread, and read it back a few frames later to check that the ball kept it
(written to the facts file for every shot, with "practice" or "online game"). Still unknown until then: how the game releases the ball,
whether the game (or the network system) re-applies its own velocity afterwards, who owns the ball in an online game, the ball's real gravity / drag,
and whether it works in BOTH practice and online games.
