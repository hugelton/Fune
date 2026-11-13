# Installing Fune on an FM-1

(Fune was called Kreisel: the files and the tools still use that name.)

Written from how Felucca 2.0 installs (`BUILDING.md`, `web/`, `tools/fm1_install.py`, `firmware/loader/`,
`firmware/src/ota_hw.c`) and checked against Kreisel's package. It has been used on a device since 2026-11-05 (in
development, nothing released yet). The install is the user's step; no tool in this repository flashes anything by itself.

## How Felucca does it, and what Kreisel shares

| | Felucca 2.0 | Kreisel | Same? |
| --- | --- | --- | --- |
| Route | USB-MIDI SysEx, the M-UPGRADE protocol of M-VAVE's own updater | the same (`third_party/felucca-bsp/chip/ota.c`, linked into the app) | yes |
| Tool | web installer (Chrome/Edge, Web MIDI) or `tools/fm1_install.py` | `third_party/felucca-bsp/tools/fm1_install.py` (the same program, MPL-2.0 copy) | CLI only: see below |
| Package | `.fwsc`: flash head + app area + the update loader (`ota.bin`), built by `tools/fm1pkg_make.py` | `.fwsc` built by `third_party/felucca-bsp/tools/fm1pkg.py` (the same code) and `tools/fm1_loader_wrap.py` | yes (`load_package` of the installer accepts `build/fm1/kreisel.fwsc` offline: identity `FM-1_1300`, model `FM-1`) |
| App address / slot | `0x02000120`, 0x8DFBC bytes | the same (`fm1pkg.py APP_SLOT`; `tools/fm1_image.py` checks the size) | yes |
| Loader | RAM image at `0x01C0A800`, staged at flash `0xE0000`, run by the SPL; it may write the app area `[0x4000, 0x97000)` and nothing of Felucca's store `[0x97000, 0xE0000)` | the BSP's `loader/`, unchanged | yes. Kreisel's songs and presets live in `[0xEA000, 0xFC000)`, outside the app, so an update keeps them |
| OTA entry in the app | `FELUCCA_OTA`: the handshake answers the identity; `ota_session` pulls the package, stages the loader, writes the record, resets | on (`fm1_main.c`); the audio is stopped before the flash is erased | yes |
| Identity | `FM-1_9XY` in the app (`FELUCCA_ID`) and in the package; checked after the install | `FM-1_1300` in both (`fm1_main.c`, `build.sh --product`) | yes, but the number is Kreisel's own pick |
| Boot guard | two boots that never get healthy -> UBOOT | the same, 2 s | yes |

Because the protocol, the package and the loader are the same, **Kreisel installs over the stock firmware, over
Felucca and over an earlier Kreisel the same way**: the installer matches only the model (`FM-1`), not the number.

## What differs

1. **The web installer is Felucca's.** `web/index_pkg.html` installs Felucca's released package and builds it from the
   official V15 file; it is not a general package installer. Use the command line for Kreisel. (A page for Kreisel
   would be `web/fm1ota.js` and `fm1pkg.js` under a new page; not started. They are GPL-3.0 in Felucca, so it would be
   written afresh against the BSP, like `fm1_loader_wrap.py`.)
2. **The package is made from scratch**, not by patching the official V15 file. That is what Felucca's own release
   does too (`fm1pkg_make.py`: "no vendor files"), so the layout is the same. The three SDK files in it are
   Apache-2.0; ship `third_party/felucca-bsp/LICENSES/Apache-2.0.txt` if you pass a package on.
3. **The loader presents as "Felucca Update"** (`ota-FM-1_900`, USB id `1209:0002`) while it works. The installer
   finds it by the `ota-` prefix and a marker (`FELUCCA-LOADER-1`) that Kreisel's package carries.
4. **Songs, presets and the settings are kept in flash** (`[0xEA000, 0xFC000)`, outside the app, so an update keeps them;
   a stock or Felucca install writes that area back). Save before an update anyway (`fm1_con.py save`): it reboots.

Nothing needed changing in the build for the install path.

## Steps (macOS)

Build, once:

```
platforms/fm1hw/build.sh                    # needs Docker running (colima start) and the JieLi toolchain and SDK
```

Install tools, once:

```
python3 -m venv ~/fm1-venv && ~/fm1-venv/bin/pip install mido python-rtmidi
```

Install:

1. Connect the FM-1 with a **USB data cable** (not a charge-only one) and close other apps that use MIDI (a DAW).
2. Check it is seen:
   ```
   ~/fm1-venv/bin/python third_party/felucca-bsp/tools/fm1_install.py --info
   ```
   It prints the identity of the FM-1 (the stock firmware, Felucca or Kreisel).
3. Install:
   ```
   ~/fm1-venv/bin/python third_party/felucca-bsp/tools/fm1_install.py build/fm1/kreisel.fwsc
   ```
   Step 1: the app pulls the package, checks it and stages the loader (a progress bar runs along the bottom of the
   screen on a Kreisel/Felucca device). Step 2: the FM-1 restarts into the loader, which writes the app; it restarts
   again and the installer checks that the device reports `FM-1_1300`. Do not unplug until it says it is done.
   Exit codes: 3 not found, 4 connection lost, 5 timeout, 6 wrong model or another identity afterwards.
4. First start: hold **HOME while powering on** (no sound), so a slow audio path cannot get in the way. Expected: the
   UI on the screen, keys lighting their LEDs. Then try without HOME held.

If an install is cut off, the FM-1 stays in update mode; run the same command again and it finishes the write.

## If it does not start

- Two power-ups in a row that do not reach a healthy loop (2 s) put the device into UBOOT (the chip's update mode).
  Then run the installer again with a package that works, or:
- `third_party/felucca-bsp/tools/fm1_rescue.sh` (macOS; needs the unmodified official `FM-1.fwsc` V15 from M-VAVE,
  SHA-256 `db1642b2…`) puts the stock firmware back, backing up the whole flash first. Read its header before use.

## The bootloader: keep the stock one

Use M-VAVE's own (stock) bootloader, on the FM-1 and on the SMK-37 alike. Fune is built and tested on
it, and the rescue paths above (the installer over USB MIDI, UBOOT with M-Flasher) are the stock bootloader's.

Fune enforces it (2026-11-13, `platforms/fm1hw/fm1_bootcheck.c`):
- **at start**, it reads the boot area and checks two things before it touches the flash:
  - the boot container must be a sound JieLi boot file, and m-bootloader's "AC79BLID" marker refuses it at once (one
    that is sound but not among the stock ones Fune knows passes, shown as `stock=0`: newer units with the UC25HQ80 flash
    may carry a newer M-VAVE bootloader)
  - `app_dir_head` must be at `0x4000`, and its XIP window must start at flash `0x4000`

  If not, it writes nothing: no songs, settings, plugins or updates. The screen says "Unknown bootloader", "Unknown flash
  layout" or "Started from elsewhere". The console then answers only `boot`, `ver`, `usb`, `memr`, `crash` and `uboot`
  (the chip's own download mode, for M-Flasher).
- **in its update loader**, which M-VAVE's firmware also starts for a first install, the same check of the boot area runs
  before anything is written: on another bootloader the update ends with an error (-51 and up) and the old firmware stays.

`boot` on the console shows what was read (`stock=1 mapped=1 verdict=0` on an FM-1 as M-VAVE ships it). Its `boot` line from
a newer unit (`stock=0`) helps to add that one to the list. fm1-dock puts its own modules where
Fune keeps its plugins (`0x96000` and up) and boots apps from `0x15000`, so the check refuses it as "Started from elsewhere"
or "Unknown flash layout".

Replacements such as [m-bootloader](https://github.com/jvitkauskas/m-bootloader) (MIT, a recovery menu) are **not
recommended**. On paper Fune should boot and update under it: it reads the same image layout, the same update request at
`0x01C7FD88` and the same `usb_update_mode` marker. But it has not been tried with Fune, and it has had little testing on
the FM-1 (the newer flash chip `0xB36014` least of all). Whoever installs one anyway does so at their own risk, with
M-Flasher rather than through Fune's own update (which may stop at `FAILED -5`); M-VAVE's official updater puts the stock
bootloader back.

## Open questions

- The installer's identity check and the SPL were not tried with `FM-1_1300`; the number is only a digit string in the
  installer (`[^_]+_\d+`). If a device refuses it, change `FELUCCA_ID` (`platforms/fm1hw/fm1_main.c`) and `--product`
  (`platforms/fm1hw/build.sh`) together.
- Whether macOS shows the loader's MIDI port under a name the installer's port filter accepts (`fm-1|felucca|ota|...`) is
  the same as for Felucca; untested here.
