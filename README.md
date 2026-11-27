# steno-fw

An original offline stenography trainer for the **LilyGO T-Embed CC1101** (ESP32-S3, 16 MB flash / 8 MB PSRAM, ST7789 320×170 display). Designed for the wired Polyglot running Gemini PR and the Lapwing English theory.

**Status: development build, not validated on physical hardware.** The portable practice engine has automated tests. The LilyGO USB-C port is not a verified USB host power source; the default build is a USB **device**, not a host. Native CDC host support is experimental, compile-tested separately by CI when available, and must not be used with an unverified direct cable or power arrangement.

## What's included

- Six original practice groups: left hand, right hand, vowels/chords, short words, everyday briefs, and longer outlines.
- Real-time stroke validation, retry, reveal-next-stroke hints, completion screen, stroke accuracy, word-per-minute estimate.
- Accepts exact expected outlines and alternate single-stroke plain-text dictionary matches.
- Mistake review sorted by historical misses and lifetime progress stored locally in NVS. Demo attempts never change progress.
- Free writing: longest-match translation (up to six consecutive strokes), visible unknown outlines, and `*` undo.
- Pinned, flash-resident **Lapwing base dictionary** generated at build time; no network or SD card needed during use.
- Gemini PR six-byte decoder with synchronization, dual-key normalization, and diagnostic view.
- Rotary encoder menu and side-button control; no radio functions, online service or account.
- Default firmware accepts Gemini packets from a local USB serial bridge; built-in demo mode works without a keyboard.
- Optional USB CDC host implementation for future compliant hardware evaluation; no VBUS sourcing implementation.

This is a trainer, not a full Plover replacement. Dictionary entries with macros, formatting commands, non-ASCII content, punctuation-only translations and numeric outlines are omitted. Free writing is limited to plain-text translations and a rolling 128-stroke history. Drills are original practice material grouped by skills rather than a reproduction of the Lapwing textbook or exact chapter numbering. WPM counts completed translated words (not raw key drills).

## Safe first test

Use only the serial-device build on the normal USB port. **Do not connect Polyglot directly to LilyGO, force VBUS, modify the board or use an improvised powered cable.** The board's BQ25896 contains a boost function, but the documented USB-C CC sink resistors do not establish a compliant source role. Keyboard current consumption and the complete host-side power path have not been validated. The firmware never enables boost; it verifies that boost is off and caps the charger current at no more than 576 mA, without enabling charging or changing the charger safety timer, temperature protection or target voltage. It refreshes the chip watchdog every five seconds. This is a software safeguard, not a substitute for hardware validation. A PMU error stops practice and displays an unplug/stop warning; discontinue testing if this occurs.

1. Confirm the exact board is **T-Embed CC1101**, not the older T-Embed. Back up current flash and obtain the official recovery firmware before flashing. Have supervision for the first board test; do not charge unattended using an unvalidated build.
2. Use the [Actions](https://github.com/CloudWaddie/SigmaTracker/actions) tab to find a **successful** Build steno-fw run. Download the serial-device artifact, unzip it, and verify the SHA256 against SHA256SUMS. If no successful run exists, do not treat the source as ready to flash.
3. With the Polyglot disconnected, use Espressif's normal ESP32-S3 flashing procedure on an authorized computer. The merged serial-device binary starts at address `0x0`. Flashing changes the bootloader/partition table and app; restore the saved flash if needed.
4. First boot on the LilyGO: verify the screen, encoder, menu and PMU check. In **Diagnostics**, verify the dictionary count is nonzero and PMU check is OK.
5. In the home menu, select **Switch input mode**; DEMO appears in the top right. Try a lesson. Click the knob for the expected stroke, turn for an intentionally wrong demo stroke, and hold to go back. Demo attempts do not save.

## Test real keyboard through a standard USB host

This is an interim, electrically ordinary arrangement:

```
Polyglot USB device ---- USB host (computer) ---- LilyGO USB device
                             serial bridge
```

Each device uses its own normal USB cable, connected to the computer. No direct device-to-device cable or external VBUS injection. Set the Polyglot to its Gemini PR mode, not QWERTY or TX Bolt. Close any other program using its serial port.

```sh
python -m pip install pyserial
python tools/serial_bridge.py --list
python tools/serial_bridge.py --keyboard PORT_OF_POLYGLOT --display PORT_OF_LILYGO
```

Switch the on-device mode to LIVE. Open Diagnostics, press a chord and verify its outline. Then start a lesson. Press the side button for a hint; click to restart the current outline; hold knob to go back. Free writing uses the side button to clear.

An independent direct USB host interface on the LilyGO remains a separate hardware-validation project. The optional host source is compiled only with `CONFIG_STENO_USB_HOST=y`; it doesn't power a keyboard and is not distributed as a flash artifact.

## Build from source

Requirements: ESP-IDF **v5.4.2**, Python 3, internet access for initial dependency and dictionary downloads.

```sh
bash tools/test.sh
# Optional full-dictionary regression suite: bash tools/test.sh --full
python3 tools/build_dictionary.py
# Run inside an ESP-IDF v5.4.2 environment:
idf.py set-target esp32s3
idf.py build
```

The host test uses the small illustrative seed dictionary, which is not authoritative Lapwing. Running the generator without arguments downloads the exact pinned upstream source, checks its Git blob SHA-1, filters plain translations and generates the actual firmware dictionary and lessons. For offline development only, use `python3 tools/build_dictionary.py --seed`. Rerun the full dictionary generator after executing the seed tests before building production firmware. Use `--source path/to/lapwing-base.json` to build from a local copy.

## Controls

| Input | Menu | Lesson | Free writing |
|---|---|---|---|
| Turn knob | Navigate | Demo: intentionally wrong stroke | — |
| Click knob | Open | Live: restart outline; demo: correct stroke | Demo sample |
| Hold knob | Back | Back | Back |
| Side button | — | Toggle hint | Clear text |
| Steno stroke | — | Practice | Translate; star undoes |

## Sources and acknowledgements

- [LilyGO T-Embed CC1101](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101): board schematic, pinout, LCD configuration and charger limit.
- [Texas Instruments BQ25896 datasheet](https://www.ti.com/lit/ds/symlink/bq25896.pdf): charger and watchdog registers.
- [Lapwing dictionary](https://github.com/aerickt/plover-lapwing-aio/tree/4ac5d53cf039a2b1f4c484456663c85e7df4e859/plover_lapwing/dictionaries), MIT license, copyright (c) 2024 Aerick. The notice is retained in `data/LAPWING-LICENSE.txt`.
- [QMK steno documentation](https://docs.qmk.fm/features/stenography): Gemini PR framing and steno mode.
- [Espressif USB host CDC ACM driver](https://components.espressif.com/components/espressif/usb_host_cdc_acm/versions/2.1.0/readme): optional transport.
