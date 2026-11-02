# Восстановление FM-1

Если после прошивки экран FM-1 чёрный и он не запускается — это ещё не конец. В FM-1 есть режим обновления
самого чипа (**UBOOT**), из него можно вернуть заводскую прошивку. Заводская область загрузчика при прошивке с
нашего сайта не трогается.

## 1. Что видит компьютер

Подключите FM-1 кабелем напрямую и посмотрите:

- **Сайт находит FM-1** (кнопка «ПОДКЛЮЧИТЬ» на https://joolo09.github.io/amen-fm1/ показывает устройство или
  «РЕЖИМ ОБНОВЛЕНИЯ») — просто прошейте заново с сайта. Если запись прервалась, она продолжится.
- **Появилось устройство `WL80UBOOT1.00`** (в системе может показаться новый «диск» или неизвестное USB-устройство
  4C4A:8057), а MIDI-порта FM-1 нет — FM-1 в режиме UBOOT. Переходите к шагу 2.
- **Ничего не видно.** Выключите FM-1, включите. Если прошивка падает при старте, после двух неудачных запусков
  FM-1 сам уходит в UBOOT. Подождите 10–20 секунд и смотрите снова. Можно выключить и включить ещё раз.

## 2. Заводская прошивка через UBOOT

Нужен **файл заводской прошивки V15**: m-vave.com → Download → FM-1 → PC Firmware, файл `FM-1.fwsc`.
Положите его в «Загрузки».

Инструмент — `fm1_rescue` из [felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0). Он:
- сначала читает всю флеш-память и сохраняет резервную копию;
- проверяет, что это FM-1 (ключ чипа 980F, флеш 1 МиБ);
- пишет только область прошивки (0x4000–0x93000);
- после записи читает всё обратно и сверяет.

### macOS

Одна команда в «Терминале»:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/hugelton/felucca-bsp/main/tools/fm1_rescue.sh)"
```

Скрипт поставит что нужно, найдёт `FM-1.fwsc` в «Загрузках», дождётся FM-1 в режиме UBOOT и спросит пароль
(sudo нужен, чтобы забрать устройство у macOS).

### Linux

```bash
python3 -m venv ~/fm1-rescue && ~/fm1-rescue/bin/pip install pyusb libusb-package
```

```bash
curl -fsSL -o ~/fm1-rescue/fm1_rescue.py https://raw.githubusercontent.com/Joolo09/amen-fm1/claude/clever-keller-ekaj8l/tools/rescue/fm1_rescue.py
```

```bash
sudo ~/fm1-rescue/bin/python ~/fm1-rescue/fm1_rescue.py ~/Downloads/FM-1.fwsc
```

Без `--write` скрипт только проверяет устройство и сохраняет резервную копию. Если всё в порядке, запустите ту же
команду с `--write` в конце.

### Windows

Инструмент рассчитан на macOS. На Windows он должен работать через драйвер WinUSB, но я это не проверял:

1. Установите [Zadig](https://zadig.akeo.ie/). Пока FM-1 в режиме UBOOT, выберите в нём `WL80UBOOT1.00`
   (Options → List All Devices) и поставьте драйвер **WinUSB**.
2. Установите Python 3 с python.org, затем в командной строке:
   `py -m pip install pyusb libusb-package`
3. Скачайте [`fm1_rescue.py`](https://raw.githubusercontent.com/Joolo09/amen-fm1/claude/clever-keller-ekaj8l/tools/rescue/fm1_rescue.py)
   (копия felucca-bsp с одной правкой: проверка прав root, которой на Windows нет) и запустите: `py fm1_rescue.py %USERPROFILE%\Downloads\FM-1.fwsc`,
   затем то же с `--write`.

Если ничего не выходит, есть аппаратный путь: [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter)
(плата RP2040 и три провода к USB FM-1).

## 3. После восстановления

FM-1 запустится с заводской прошивкой V15. Дальше можно ставить прошивку с сайта.
