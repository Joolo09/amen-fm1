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
- `firmware/src/params.c` (N_ENGNAME), `firmware/src/icons.c` (the engines' icons; TRIO's MODE icon case removed),
  `firmware/src/ui_graph.c` (the 4-letter tags JNGL, RESE): the names of engines 2 and 6.
- `firmware/src/ui_menu.c`: the credits start with AMEN.
- `firmware/src/core.h`: AMEN_SETUP (1: the jungle setup; 0: Felucca's tracks and 120 BPM, for Felucca's host tests).

## Sample data reads

UBSan reported `eng_slice.c:134: index 8640448 out of bounds for type 'uint8_t [126244]'` (`tests/ui_render.c`, the
SLICES page on a user slot). No position was wrong: at the report the decoder is at sample 0 of the slot's only zone.
Felucca reads a user slot's data (flash 0xA0000..) as `SMP_DATA[off]`, `off` rebased onto SMP_DATA (`eng_sample.c`
usr_zone), so for a user slot the index always lies past the array: the address is the slot's bytes, but indexing
outside an array is undefined in C. SAMPLE, GRAIN and the waveform view read the same way (`sample_test` with PIANO HD
in USR1: `eng_sample.c:141`). Felucca's main after 1.9.0 has the same code; its ASan / UBSan runs leave bounds out.

- `firmware/src/eng_sample.c`: `smp_bytes()`, SMP_DATA's address through an empty asm, so no object is attached (as
  Linux's RELOC_HIDE; `bsp/chip/fm1_cpu1_stop.h` does the same): the same load, out of the inner loops as before (x86
  -O2: the loops no larger). `eng_sample.c` (SAMPLE), `eng_grain.c` (GRAIN), `ui_graph.c` (the waveform) and
  `eng_slice.c` (SLICE) read through it.
- `firmware/src/eng_slice.c`: the decoder reads only inside a zone of the material (`slc_dec`). A position no zone
  holds (past the end, wrapped, a zone index past the zones) reads nothing and the voice ends there: forwards at
  once, REV once the window it already decoded (its own RAM) is played out. The zones past `nseg` are cleared at a
  scan; a decoder state enters in range (step index <= 88, zone < 16); the REV window's grid index is clamped as in
  `slc_state_at`. `slc_dec_next` keeps its form for the scans, the SLICES page and Felucca's tests (a refused read
  still steps, so their loops end). Valid positions play bit for bit as before: SLICE's 9 and SAMPLE's 24 demos
  (`slice_test`, `sample_test`) are identical, AMEN_SETUP 0 and 1.
- On the device no voice reaches a bad position in normal play: an engine switch ends the part's voices before the
  new engine renders (`voice.c` engine_block); an upload's BEGIN empties the slot (`usr_nz` 0; WRITE refuses a slot in
  use) and every part renders every block, muted or not, so each voice on it ends before END rescans it;
  `smp_user_scan` runs on a valid slot only at boot. The bound guards what was never proven impossible, at no cost
  per sample (the check is in the zone change's branch).
- `tests/amen_slice_bounds_test.c` (new), `../tools/amen_slice_check.sh` (new, CI): ASan + UBSan (but shift: the DSP
  shifts negative values, as Felucca's drumfuzz run) on a slot whose data end where its image ends: every slice
  (DIV 4..AUTO, forwards and REV, ONE and LOOP across the zones), bad positions and a material replaced under a voice
  stop; then `slice_test`, `sample_test` and `ui_render` under UBSan with bounds: no report. Felucca's suite on AMEN
  stops at `fold155_test` (`set -e`), so `tools/felucca_tests.sh` never reached `slice_test` / `sample_test`.

## The AMEN interface (AMEN_SETUP 1)

- `firmware/src/ui_amen.c` (new, included by `ui_graph.c`): the BREAK MAP (a JUNGLE track's HOME and EDIT pages: the
  break's 32 slices as transients, the slice sounding lit and named), the REESE picture (the oscillators' beating,
  the sub), CHOP (a JUNGLE track's sequencer: steps as slices with their hits, rolls, chances) and BASSLINE (a REESE
  track's sequencer: notes as bars at their pitch, ties, slides as glides, accents, the notes' names).
- `firmware/src/ui_amen_tools.c` (new, included by `ui_tools.c`): SEQ TOOLS on a JUNGLE track: RANDOM is JUNGLE (the
  break with classic edits), COOK is BRKCORE (a mangle), BEAT is ROLLER (drum & bass two-step from the break's hits).
- `firmware/src/ui_graph.c`: the graphs above in place of the scope / piano roll on those tracks; the footer's title
  CHOP / BASSLINE; the redraw signature follows the slice sounding.
- `firmware/src/ui_draw.c`, `ui_input.c`: CHOP's knobs: KNOB 2 SLICE (round the 32, its hit named: KIK SNR GHO HAT ..),
  KNOB 4 ROLL (x1 .. x4); `firmware/src/icons.c`: their icons. AMEN's own splash (the name, the break's first bar,
  the styles, the version).
- `firmware/src/core.h`, `engines.c`: six engines offered (JUNGLE REESE DRUM SLICE ANALOG FM6); the others stay built
  so a project with them still plays.
- `tools/gen_ui_palettes.py`, `firmware/src/panel.c`: the AMEN palette (near-black, acid yellow-green, hot pink),
  the default.
- `tests/ui_render.c`: AMEN's scenes (amen_*: the break map playing, the reese, CHOP, BASSLINE, the edit makers), the
  AMEN palette's PNGs, the splash's expected lines.

## The 80s look (AMEN 0.2)

Every screen drawn in its own style, not Felucca's:

- Font: Chakra Petch (SIL OFL 1.1) in place of Inter Tight: `tools/gen_aa_font.py --preset chakra-petch`
  (`tools/build.py` uses it), on Inter Tight's line heights (S 15, M 19, L 35: Felucca's layouts are measured on them;
  every glyph's ink fits, the generator asserts it), 2 % tighter, no kerning inside numbers (even pitch).
- Palette AMEN: synthwave night: deep indigo, neon magenta values, lavender-white text, cyan for the active thing,
  sunset orange for REC (`tools/gen_ui_palettes.py`, every contrast check passed).
- `firmware/src/gfx.c`: the cards' corners cut at 45 degrees (as Chakra Petch's), 2 px bigger; `ux.bw` (MONO).
- `firmware/src/ui_draw.c`: the knob cards' gauges as LED meters (4 px segments); the header's neon horizon (its bottom
  two rows, THEME to ACCENT, drawn by every header canvas, `ui_menu.c` too); the splash: a synthwave sunset drawn in
  code (sky, stars, the cut sun, the grid), the chrome AMEN logo and an original 80s anime DJ as sprites
  (`firmware/src/amen_art.h` from `../tools/gen_amen_art.py`, 10 KB), the styles and the version on a label; in GREY
  and MONO all in grays. `firmware/src/main.c`: the splash shows 1.5 s.
- `firmware/src/ui_draw.c`: the header's edited mark 1 px further from the slot letter (the wider A).
- `tests/ui_render.c`: the splash's lines (the logo is art). `../tools/amen_ui_check.sh` (CI): every AMEN screen in
  every palette through Felucca's renderer: 0 layout findings, GREY / MONO gray, alignment as Felucca's (532 offsets).
