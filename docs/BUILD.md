# Сборка

## На ПК: тесты, демо, десктопные плагины

Нужны C99-компилятор, `make` и Python 3. Больше ничего.

```bash
make test
```

```bash
make demo breaks
```

```bash
make plugins
```

- `make test` собирает `build/amen_host` (десктопный хост, который вызывает движки как Felucca: голоса партии, хуки `engine_t`, огибающая раз в блок из 32 сэмплов, те же таблицы) и прогоняет проверки.
- `make demo` → `build/demo.wav`, `make breaks` → `build/break0.wav` … `break7.wav`.
- `make plugins` → `build/plugins/jungle.so`, `reese.so` для десктопной Felucca (SDL2): `felucca --plugins build/plugins`.

Таблицы (`SINE`, `PITCH_OCT`, `SVF_G`, `TANH_Q15`) пишет `tools/gen_tables.py` по тем же формулам, что у Felucca. Поэтому рендер на ПК совпадает с FM-1 побитно, если совпадает целочисленная арифметика (её движки соблюдают: только 32-битные произведения, без float).

## Для FM-1: файлы `.fep`

Нужен **тулчейн JieLi** (clang 4.0.1 для pi32v2), Linux x86-64 или Docker. Felucca ставит его своим скриптом:

```bash
git clone --depth 1 https://github.com/hugelton/Felucca ~/Felucca
```

```bash
sh ~/Felucca/tools/get_toolchain.sh
```

Тулчейн окажется в `~/.jieli/toolchain`; другой путь задаётся через `JIELI_TOOLCHAIN`. Затем:

```bash
make fm1
```

Получатся `build/fm1/jungle.s0.fep` (слот 0) и `build/fm1/reese.s1.fep` (слот 1). Каждый файл слинкован под свой слот. Чтобы поставить движок в другой слот, соберите его под этот слот, например `sh tools/build_fm1.sh jungle 1`.

Лимиты слотов (проверяет `sdk/tools/fep_pack.py`; он откажется паковать то, что FM-1 не примет):

| | Слот 0 | Слот 1 |
|---|---|---|
| Код и константы | 8 КБ − 96 Б | 7,5 КБ − 96 Б |
| `.data` + `.bss` | 4 КБ | 4 КБ |

Фактические размеры (сборка в GitHub Actions, тулчейн jieli-linux-toolchains-20260730.1, clang 4.0.1): JUNGLE — 4712 Б из 8192, REESE — 3000 Б из 7680, RAM 8 Б у каждого.

**GitHub Actions** (`.github/workflows/build.yml`) на каждом пуше прогоняет `make test`, рендерит демо, скачивает тулчейн JieLi и собирает оба `.fep`. Результаты лежат в артефактах запуска: `amen-fm1-fep` (плагины для FM-1) и `amen-fm1-demo` (WAV и десктопные `.so`). Ничего ставить у себя не нужно.

Установка на устройство (утилита из репозитория Felucca, `pip3 install mido python-rtmidi`):

```bash
python3 ~/Felucca/tools/fm1_plugin.py install build/fm1/jungle.s0.fep
```

```bash
python3 ~/Felucca/tools/fm1_plugin.py install build/fm1/reese.s1.fep
```

```bash
python3 ~/Felucca/tools/fm1_plugin.py info
```

## Что проверить на железе

1. `fm1_plugin.py info`: оба плагина загружены, не «skipped after a crash».
2. JUNGLE на ручке ENG, все 14 пресетов звучат, клавиши F3… дают слайсы 0, 1, 2…
3. Нагрузка: 8 голосов JUNGLE в режиме POLY с крэшами и райдами плюс REESE с 4 голосами. Если Felucca начнёт сбрасывать голоса (просадки CPU), самые дорогие места такие: шесть меандров тарелок в слайсах с R/C и фильтр TONE < 127. Их можно облегчить.
4. Режимы CHOKE / GATE / POLY при записи в секвенсор и с ратчетами.
5. Громкость относительно движков Felucca: JUNGLE рассчитан на уровень голоса `VOICE_FS` (пик около −6 дБFS на голос).

## Своя прошивка

Плагины не трогают прошивку. Если нужна именно своя прошивка (например, загрузка сразу в «jungle-режим»), сделайте форк Felucca и вкомпилируйте движки внутрь. Исходник `engine_t` для этого тот же:

1. Форк [Felucca](https://github.com/hugelton/Felucca), скопировать `engines/eng_jungle.c`, `engines/eng_reese.c`, `engines/jungle_data.h` в `firmware/src/`.
2. В `firmware/src/engines.c` добавить `#include "eng_jungle.c"` и `#include "eng_reese.c"` рядом с другими движками и вписать `&ENG_JUNGLE`, `&ENG_REESE` в `ENGINES[]` под свободными номерами (и поправить `NENGINES`/`NENG_BUILTIN` в `core.h`). Номера 15…31 Felucca держит за собой под будущие движки: в форке это ваш выбор, но проекты с этими номерами не будут совместимы с основной Felucca.
3. Поменять стартовый набор треков (`TRK_DEF` в `engines.c`) на JUNGLE + REESE и стартовый темп на 170.
4. Собрать прошивку по [BUILDING.md](https://github.com/hugelton/Felucca/blob/main/BUILDING.md) Felucca (тот же тулчейн, плюс три файла из JieLi AC79 SDK) и поставить её через её же веб-установщик.

Felucca распространяется под GPL-3.0-only, значит и форк тоже. Движки этого репозитория под той же лицензией, так что с этим проблем нет.
