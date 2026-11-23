# Robotron: 2084

This plugin plays Williams' 1982 Robotron: 2084 arcade game on the ColecoVision. It needs your own copy of the game's ROMs.

## Setup

1. Copy `ROBOTRON.CVP` into the `/PLUGINS` folder on the card.
2. Put the ROMs from MAME's `robotron` set in a folder named `/ROBOTRON`. You can copy the 13 files loose, or copy `robotron.zip` as it is. You can also put the zip in the card's top folder as `/ROBOTRON.ZIP`, but the high scores need the `/ROBOTRON` folder.
3. Start Robotron: 2084 from the Plugins page in the menu.

The files can have any names, because the plugin knows each ROM by its size and contents. If a ROM is missing, the screen names it. Only MAME's `robotron` set works. The clones have different ROMs.

## Controls

Robotron is a twin-stick game. With two ColecoVision controllers:

- Controller 1's joystick moves the man.
- Controller 2's joystick fires, in the way it points.

With one controller:

- Hold the left fire button to fire the way you're moving.
- Hold the right fire button to stand still and fire the way the joystick points.

Either way:

- Keypad * or # adds a coin.
- Keypad 1 starts a one-player game, and keypad 2 a two-player game.
- Press RESET on the console to go back to the menu.

On a USB pad, through a powered hub:

- The D-pad or the left stick moves.
- The right stick fires. So do the face buttons: top fires up, bottom down, left left and right right, and two at once fire on a diagonal.
- L and R work like the left and right fire buttons.
- Minus adds a coin, and plus starts a one-player game.

Keypad 0 is the arcade board's Advance button, and holding keypad 9 sets its Auto Up switch. Together they open the operator's menus.

## The picture and the sound

The scores and the wave number stay at the arcade's size, at the top and bottom of the screen. The play field is scaled to fit between them, a little narrower and shorter than the arcade's. Between waves the whole arcade screen is scaled to the TV.

The sound is the arcade board's own, played as samples on the ColecoVision's sound chip.

## High scores

The arcade board keeps its high scores and settings in a small battery-backed memory. The plugin keeps it in `/ROBOTRON/NVRAM` on the card. That's the same file MAME keeps in `nvram/robotron/nvram`, so you can copy yours across in either direction. The plugin writes it once a game is over and the initials are in.

The first time, the plugin makes the file, and the game restores its factory settings.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Aaron Giles, for the Williams driver, its blitter and video, the 6800 CPU family (which plays the sound) and MAME's screen timing.
- Nathan Woods, for the 6809 CPU, and with Aaron Giles for the 6821 PIA.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license. The plugin is built with the Raspberry Pi Pico SDK (BSD-3-Clause) and the Parrot Cart SDK (MIT).

The plugin itself is by Parrot Cart, and it's free. Its source is linked next to its download.

Robotron: 2084 is a trademark of its owners. This plugin contains none of the game.
