***  HEAVY GEAR 1.2  ***


WHAT DOES 1.2 CHANGE OVER 1.1?

* New textures have been added for the 3D hardware version.

* The particle system has been enhanced for all 3D hardware modes.

* New lighting has been added for weapon firing and contrails.

* The clipping plane has been pushed out for added visibility.

* Particles on footsteps have been added.

* Added additional features for Voodoo 2 users.

* Plus  bug fixes like:

1. 	In mission 6 of storied version of Heavy Gear, the truck is no
        longer stuck in midair.

2. 	When your Gear is legged, the game no longer crashes.

3. 	Riva 128 Flicker problems in D3D mode (see below troubleshooting
        section).

4. 	There is now a Heavy Gear option when playing on MPlayer.

5. 	Joystick (especially Microsoft Sidewinder 3D Pro) works.

6.	Screenshots now work on any 3Dfx card.

7.	Text wrap/flicker in the mission objective box.

8.	Problems with passworded network games are fixed.

9.	Low-light crash fixed.
  


Patch 1.2 trouble shooting:


PROBLEM
The screen sometimes flickers when I run Heavy Gear 1.2 on my NVIDIA
RIVA (Diamond Viper 330, STB Velocity 128) video card.

SOLUTION
We have found that older display drivers may cause this problem.
Install the latest drivers from the respective card manufacturer
without the Vision utilities.



PROBLEM
After installing the game on my computer with an Orchid Righteous 3Dfx
card, the game crashes when I try to launch.

SOLUTION
You will need to reboot your computer.  You should now be able to
launch into the sim.



PROBLEM
I am having problems updating the version of Heavy Gear that came with
my Sony VAIO Computer.

SOLUTION
If you have updated the version of Heavy Gear that came with your Sony
VAIO Computer to 1.1 the 1.2 update may not execute properly.  You
will need to uninstall Heavy Gear and reinstall the original version
that came with your computer.  The 1.2 update should now install
properly.



PROBLEM
Heavy Gear runs with multiple screen images on my computer with a STB
Velocity 128 video card.

SOLUTION
Install the latest version of the STB Velocity 128 drivers without the
Vision utilities.




WHAT DOES 1.1 FIX OVER 1.0?

The 1.1 version (all changes are included with 1.2) contains the
following upgrades:

* In multiplayer games, AI players are enabled, both as squad-mates,
  and as enemies.

  NOTE: Computer-controlled Gears take up just as much bandwidth as
        human players.  The following limits should be respected, or
        else you will always experience packet loss (and poorer
        gameplay):

	Slowest Connection	Number of Players
	T1			8 players
	28.8			4 players
	14.4			2 players

* Players using the 3Dfx (Glide) graphics mode will now have improved
  object sorting, as z-buffering has been enabled in this mode.
  Z-buffering adversely affects wireframe mode (some wireframes don't
  show up).  Switch to D3D mode in order to turn off z-buffering and
  restore wireframe mode functionality.

* Force-Feedback effects have been souped-up.  We have added
  additional effects for different weapon types as well as fixed some
  problems with effects implemented in the release version.

16-Bit Textures
* For players with 3D accelerators and 4MB of dedicated texture
  memory, 16-bit textures with alpha blending have been enabled.

  Only cards with 4 MB of texture memory will have 16-bit textures
  automatically enabled. Some cards, including the Hercules Stingray
  128 6MB card, and the Intergraph Intense 3D 6MB card, have 4 MB of
  frame buffer which cannot be used as texture memory.  These cards
  will not default to 16-bit textures.

  The Canopus Pure 3D card will, however, default to 16-bit textures
  when using this patch.  Do not use the reference drivers for the
  Pure 3D card, as doing so will cause a crash. Use the drivers which
  were shipped with the card itself.

  NOTE: Due to the large number of textures in Heavy Gear (over 5
        times more than our previous games), players with only 2MB
        of texture memory on their 3D accelerator card are
        restricted to 8-bit textures with no alpha blending.


BUG FIXES

Multiplayer

* Intermittent instability after killing an enemy has been fixed
* Repeating message bar about having destroyed an enemy Gear no longer
  occurs
* Intermittent crash on multiplayer debriefing is fixed
* Session no longer ends when host exits in game with 4 or more
  players
* Gears can no longer regenerate to a start point that is still
  occupied by another Gear
* Occasional incorrect attribution of kills has been eliminated
* Delay when Internet war server isn't found has been shortened
* Text messages have been added when accessing the various servers
* Players falling out of sync during transitions from briefing screen
  to the mission and back has been made less likely, though it can
  still occur on extremely slow computers
* Problems with joining and launching password-protected private games
  have been eliminated
* Disabled space bar after death, preventing tracking of other players
  or individual regeneration, has been fixed
* Fixed instability after Strike missions

Single Player (Story Mode)

* Planes are now in the correct position when using an accelerator
  card (above the deck of the Vigilance)

General

* Intermittent cases of Gears being destroyed without changing color
  on their wireframe image has been fixed
* Warning box on Combat Variables no longer comes up at inappropriate
  times
* Intermittent crash in Pursue missions has been fixed
* Crash after deleting one remaining character on Character Selection
  screen and accepting a newly created one has been fixed
* No longer incorrectly plays Force-Feedback effects when not carrying
  a weapon
* Removed the accidental ability to get two hand-held weapons when
  entering game empty-handed
* Fixed Error 55 in Duel missions
* 3Dfx-based accelerator cards with 2 texture mapping units are now
  supported correctly in the 3Dfx (Glide) graphics mode
* Crashes with MonsterSound (and other Aureal based boards) have been
  fixed, but new drivers are required from your board manufacturer,
  e.g. refer to the Diamond web-page for latest drivers for the
  Diamond Monster Sound 3D.


Take the opportunity to check our website for future updates.

www.Activision.com


GENERAL TROUBLESHOOTING

The vast majority of problems encountered are due to obsolete drivers.
If you are experiencing strange sound or graphics, crashes, freezes,
or extremely poor performance, make absolutely certain you have the
latest drivers from your video-card and audio-card manufacturers.
Since Heavy Gear uses DrawPrimitive, a new addition in DirectX 5, not
all manufacturers had correct drivers at the time of release.  Most
manufacturers make the latest drivers available on their web-sites.
Chip manufacturers such as 3Dfx, Rendition, and nVidia generally have
reference drivers available on their own sites as well.

* If you are having trouble with movie play-back (movies crashing, 
  playing too fast, or simply not playing), it's possible that you
  have a newer version of ActiveMovie that is somehow incompatible
  with the version Heavy Gear is using.  We recommend uninstalling
  other ActiveMovie applications, such as Internet Explorer 4.x to
  test if this is a problem on your system.  It is possible to install
  IE4 without DirectShow and ActiveMovie, which has been reported to
  work with no problems. We're looking into the specific cause of this
  problem.


*** Note for international users:
If you are using a version of Windows 95 that has an IME Status Window
(a tool bar that allows you to enter Chinese or Japanese text in
Win95), you will have to disable it before running Heavy Gear.  To do
this, click on the 'pen icon' on the Win95 task bar and follow the
instructions in order to turn this off.


Multi-Player Mode to Modem Note:
When playing Heavy Gear modem to modem, you should use a 28.8 Kbs or
greater modem.



Activision is a registered trademark of Activision, Inc.
(c)1997 Activision, Inc.

Heavy Gear is a registered trademarks of Dream Pod 9, Inc.
(c) 1997 Dream Pod 9, Inc. All rights reserved. 

