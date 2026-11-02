# sdk/

Файлы из [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT, © 2026 Leo Kuroshita / Hügelton Instruments),
без изменений, коммит `0f015add84d77d8ae0ead08de901b7ecf3686f18`:

- `src/felucca_engine.h`, `src/felucca_plugin.h`, `src/felucca_plugin_fm1.h` — интерфейс движков и плагинов (ABI 1);
- `src/dsp.c` — общие DSP-блоки (синус, polyBLEP, SVF, софт-клип, шум);
- `tools/` — оригинальные скрипты сборки плагинов и упаковщик `fep_pack.py` (его использует `tools/build_fm1.sh`).

Лицензия: [LICENSE](LICENSE).
