# HARDWARE_FACTS: M-VAVE FM-1

What this project knows about the FM-1 and where each fact comes from. CONFIRMED means it is read from source code that
is known to run on hardware, or seen on the owner's device. UNKNOWN means not established. Nothing here should be used
for a flash write unless it is CONFIRMED.

## The chip and the memory map

| Fact | Value | Source | Confidence |
| --- | --- | --- | --- |
| Base | JieLi chip, felucca-bsp's FM-1 board | `fw/firmware/bsp/README.md` | CONFIRMED (source) |
| Flash part | JEDEC `0x856014`, 1 MiB; the firmware refuses flash writes on any other part | `os/fm1/main.c`, `bsp/examples/hwtest/hwtest.c` | CONFIRMED (source, hwtest runs on hardware) |
| App link address | XIP `0x02000120`, slot `0x8DFBC` bytes | `bsp/examples/hwtest/app.ld`, `os/fm1/app.ld` | CONFIRMED (source) |
| RAM | `0x01C08000`, 96 KiB for .data/.bss; stacks `0x01C74000..0x01C7C000`; `.noinit` `0x01C7C000`; `0x01C7FD50..` reserved (boot info, mailbox, vectors) | `os/fm1/app.ld` | CONFIRMED (source) |
| Felucca main store | flash `0x97000..0xE0000` | `bsp/chip/fm1_flash.h` `FL_DATA_*` | CONFIRMED (source) |
| Update loader staging | flash `0xE0000..0xE5000` (5 × 4 KiB) | `bsp/chip/ota.c` `OTA_AREA*`, `fm1_flash.h` `FL_OTA_*` | CONFIRMED (source) |
| Felucca autosave | flash `0xE5000..0xE7000` | `fm1_flash.h` `FL_AUTO_*` | CONFIRMED (source) |
| Felucca globals | flash `0xFC000..0xFF000` | `fm1_flash.h` `FL_GLOB_*` | CONFIRMED (source) |
| AMEN OS project store | two copies at `0x97000` and `0x99000` (inside Felucca's main store) | `os/fm1/main.c` `ST_A`/`ST_B` | CONFIRMED (source); NOT RUN on hardware |

## Update and recovery paths

| Fact | Value | Source | Confidence |
| --- | --- | --- | --- |
| USB-MIDI update | the running firmware answers an identity query (`F0 00 32 45 … F7` → `…59 11…` "MODEL_NNN"), then the M-UPGRADE session stages the loader; the loader ("ota-FM-1") writes the image | `site/vendor/fm1ota.js`, `bsp/chip/ota.c` | CONFIRMED (Felucca's installer) |
| Packages the site writes | `.fwsc` built by `fw/tools/fm1pkg_make.py`; identity `FM-1_9XY` (AMEN on Felucca), `FM-1_7XY` (AMEN OS) | `tools/build_amen.sh`, `tools/build_os.sh` | CONFIRMED (source) |
| Installer refuses other models | an identity whose model is not `FM-1` gets no update command and no write | `site/vendor/fm1ota.js` `install()`, `tests/site/e2e.js` | CONFIRMED (test against the mock) |
| UBOOT | the chip's mask-ROM update mode; on USB `WL80UBOOT1.00`, `4C4A:8057` | `bsp/tools/fm1_rescue.py` | CONFIRMED (source); seen on the owner's FM-1 after AMEN OS 0.2 |
| fm1_rescue | checks and backs up first, writes only with `--write`, and only M-VAVE's FM-1 V15 `.fwsc`, SHA-256 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` | `bsp/tools/fm1_rescue.py`, `tools/rescue/fm1_rescue.py`, `docs/RESCUE.md` | CONFIRMED (source); the owner recovered an FM-1 this way |
| Boot guard (AMEN OS 0.3) | 1–2 failed boots → SAFE MODE (USB + installer, no app); 3 → UBOOT | `os/fm1/main.c` | CONFIRMED (source); NOT RUN on hardware |

## Other FM-1 firmwares (read, not verified)

The community list https://github.com/aroum/fm1-custom-fw (read 2026-11-03, first 100 000 of 263 705 characters) names
over a hundred FM-1 firmwares (Felucca and its forks, Melodee, X0X, SLOOP and its forks, FoMni, …), most with their own
web installer. Details it states: stock FM-1 V16 (2026-10-21) is the latest stock release; KARMA shows as USB device
"FM-1 KARMA"; FANTASMA/Pelucco reports "FM-1_000" and has a rescue boot (OCT- at power-on). Their update paths and flash
layouts are not checked here: UNKNOWN for every one of them until read in its own source.

## The owner's FM-1

| Fact | Value | Confidence |
| --- | --- | --- |
| Hardware revision | not known | UNKNOWN |
| Firmware installed now | the site's AMEN 0.1 (Felucca-based): the owner reports it set up, 2026-11-03 ("It's set up, but it took a while"); how the "other firmware" before it was replaced was not reported | CONFIRMED (owner report; version inferred from the site) |
| Bootloader | the chip's ROM UBOOT is present (seen); whether the installed firmware changed anything above it is not known | UNKNOWN |
| A read-back backup of its flash | none on record | UNKNOWN |
| Its USB-MIDI port name, and whether it answers the update identity query | answers it (AMEN 0.1 was installed over USB-MIDI by the site); the port name not reported | CONFIRMED (inferred from the install) |
| AMEN OS 0.2 on it | black screen, then UBOOT; recovered by the owner | CONFIRMED (owner report) |
| AMEN OS 0.3 on it | never run | NOT RUN |
| AMEN 0.2 (the 80s look) on it | never run: built and rendered on a computer only | NOT RUN |
