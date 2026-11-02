# AMEN firmware

The firmware of AMEN-FM1: Felucca 1.9.0 (https://github.com/hugelton/Felucca, tag v1.9.0, the release's source
archive `felucca-1.9.0-source.tar.gz`, GPL-3.0-only) with AMEN's engines built in and set up for jungle, drum & bass
and breakcore. Everything in this folder is Felucca's, under its licences (LICENSE, LICENSING.md, LICENSES/), except
the changes listed here. The engines themselves are in `../engines/` (GPL-3.0-only).

## Changes to Felucca 1.9.0

- `firmware/src/engines.c`: engine 2 is JUNGLE (`../engines/eng_jungle.c`) instead of PHASE, engine 6 REESE
  (`../engines/eng_reese.c`) instead of TRIO; the engines' order starts JUNGLE, REESE, DRUM, SLICE; the parts at
  power-on are JUNGLE AMEN+5 with the JUNGLE edit, REESE with its line, JUNGLE BRKCORE, DRUM KIT; four patterns added
  (BREAK, JUNGLE, ROLLER, REESE: 14 .. 17).
- `firmware/src/fx.c` (ENG_VCOST), `firmware/src/midi_control.c` (MIDI_CC_ENG: JUNGLE TONE on CC 74, REESE CUT / RESO on
  CC 74 / 71), `firmware/src/category.c` (PRESET_CAT, cat_of_engine): the entries of engines 2 and 6.
- `firmware/src/params.c`: BPM 170 at power-on.
- `firmware/src/ui_draw.c`: the splash says AMEN.

Projects and user presets keep Felucca's formats; a Felucca project's PHASE and TRIO tracks play as JUNGLE and REESE
here. The editor protocol still names the firmware `FELUCCA vX.Y`, so Felucca's web editor and backups work with it.

## Build

`tools/build_amen.sh` at the repository's root (the JieLi toolchain and three files of the JieLi AC79 SDK, as Felucca's
BUILDING.md says). GitHub Actions builds it on every push: the package is `amen-X.Y.fwsc`.
