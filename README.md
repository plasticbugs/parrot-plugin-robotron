# Robotron: 2084

This plugin lets Parrot Cart play Williams' 1982 Robotron: 2084 arcade game on the ColecoVision. The cart emulates the arcade board, ported from MAME. You supply the game's ROMs from your own copy of MAME's `robotron` set.

## What goes on the card

1. Copy `ROBOTRON.CVP` into the `/PLUGINS` folder.
2. Make a folder named `/ROBOTRON` and put MAME's `robotron.zip` in it as it is, or the 13 files from it loose. You can also put the zip in the card's top folder as `/ROBOTRON.ZIP`, but the high scores need the `/ROBOTRON` folder.
3. Start Robotron: 2084 from the menu.

The plugin knows each ROM by its size and contents, so the file names don't matter. If a ROM is missing, the screen names it. Only MAME's `robotron` set works. The clones have different ROMs.

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
- RESET on the console goes back to the menu, and so does holding both fire buttons and \* for 2 seconds (Select and Start on a USB pad).

On a USB pad, through a powered hub:

- The D-pad or the left stick moves.
- The right stick fires. So do the face buttons: top fires up, bottom down, left left and right right, and two at once fire on a diagonal.
- L and R work like the left and right fire buttons.
- Minus adds a coin, and plus starts a one-player game.

Keypad 0 is the arcade board's Advance button, and holding keypad 9 sets its Auto Up switch. Together they open the operator's menus.

## Picture and sound

The scores and lives at the top and the wave number at the bottom stay at the arcade's size. The play field is scaled to fit between them: 278 by 215 pixels become 256 by 179, by merging 22 pairs of columns and 36 pairs of lines spread across it, so a line one pixel wide still shows. Between waves, and in the attract mode, the whole arcade screen is scaled to the TV.

Each 8-pixel stretch of a line can show two colours on the ColecoVision. Where the arcade has more, each pixel takes the nearer of the two most used.

The board's DAC is played as samples on the ColecoVision's sound chip, about 6,000 a second, at about the loudness of an ordinary cartridge.

## High scores

The arcade board keeps its high scores and settings in a small battery-backed memory. The plugin keeps it in `/ROBOTRON/NVRAM` on the card. That's the same file MAME keeps in `nvram/robotron/nvram`, so you can copy yours across in either direction. The plugin writes it once a game is over and the initials are in.

The first time, the plugin makes the file and the game restores its factory settings.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Aaron Giles, for the Williams driver, its blitter and video, the 6800 CPU family (which plays the sound) and MAME's screen timing.
- Nathan Woods, for the 6809 CPU, and with Aaron Giles for the 6821 PIA.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license. The plugin is built with the Raspberry Pi Pico SDK (BSD-3-Clause) and the Parrot Cart SDK (MIT).

The plugin is by Parrot Cart, and it's free. Its source is here: https://github.com/plasticbugs/parrot-plugin-robotron

Robotron: 2084 is a trademark of its owners. Parrot Cart isn't connected with them. This plugin doesn't include any game ROMs.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool. Releases are built with the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1) and picotool 2.3.1. With those you get
the same file as the release, byte for byte.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/ROBOTRON.CVP`. On Ubuntu, `tools/ci/setup.sh` installs the
same tools GitHub uses to build each release.

`CART-README.md` is the text the cart shows on the plugin's page.
`test/rb/README.md` explains how the picture and sound are made, and the
tests against MAME. The rest of this repository is the Parrot Cart starter
kit, and `docs/` is its manual.

## License

The plugin is under the BSD 3-Clause license (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
