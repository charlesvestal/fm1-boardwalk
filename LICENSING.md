# Boardwalk licensing

Boardwalk is free software under the **Mozilla Public License 2.0** (`MPL-2.0`, full text in `LICENSE`), file by
file. If you change one of its files and share the result, share that file's source under the MPL. It is built on
[fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp)
(`base/`, a git submodule), both MPL-2.0.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: `base/` (boot, USB, storage, the update path, screen, panel, the screen reader, the host simulator and the browser emulator, build and install tools) | [fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0 with his permission ([felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4)) | MPL-2.0 |
| `app/` (the shell, the games, the sound engine `dsp/bw.c` with its Dattorro plate from FoMni-1), `tests/`, `tools/`, `mockups/` | Boardwalk, Charles Vestal | MPL-2.0 |
| Change ringing's methods (`app/games/ringing.c`): their place notations | the Central Council of Church Bell Ringers' collection, methods.cccbr.org.uk; facts, written out here | — |
| The songs in Tiles and the cabinets' tunes | traditional, or composers long out of copyright (Beethoven); arranged here | public domain |
| The idea of a browser emulator for FM-1 firmware (fm1-base's emulator) | [Groove OS](https://www.groove-os.com/) by Peter Gombos; no code from it | credit only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners, used here only to say what hardware Boardwalk runs on. Simon, Flappy Bird, Piano Tiles and
Whac-A-Mole are named only to say what some cabinets are inspired by. Boardwalk is independent firmware; it is not
affiliated with, endorsed by or supported by any of them.

## Radio

Boardwalk never enables the Bluetooth / Wi-Fi radio of the hardware.
