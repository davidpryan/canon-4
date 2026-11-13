# CANON-4

CANON-4 turns one short phrase into counterpoint. Sixteen cells (a 4 x 4 field of scale degrees and rests, on the white keys) are read by four playheads at once, each at its own rate and direction, along its own path through the field (rows, columns, a snake, diagonals, a spiral, at random), from its own offset and length, transposed by its own degrees and played by its own sound. The same tune a third above at half speed, backwards an octave up, a slow bass on the columns: one edit and every voice hears it. The heads drift apart and meet again every few bars, so the variation always resolves.

![CANON-4 on the FM-1's screen](manual/img/screen-field.png "CANON-4 on the FM-1's screen")

Version {{VERSION}}. **Untested on a real FM-1 so far:** it builds, passes its tests in fm1-base's simulator, and
runs in the browser emulator. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): what it is for, the controls, the screens.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 base/tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/davidpryan/canon-4): MPL-2.0. Built on fm1-base and felucca-bsp, from Felucca by Leo
  Kuroshita, Hügelton Instruments. One of six sequencer firmwares built from a survey of sequencer archetypes.

M-VAVE and FM-1 are trademarks of their owners. CANON-4 is not affiliated with M-VAVE.
