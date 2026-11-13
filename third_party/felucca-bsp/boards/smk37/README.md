# boards/smk37: the M-VAVE SMK-37 Pro / Elite

The FM-1's chip behind another panel. These headers give the FM-1 code the SMK-37's board under the FM-1's own names, so the
FM-1 firmware builds for it unchanged (Fune: `platforms/fm1hw/build.sh` with `KREISEL_BOARD=smk37`):

| File | What |
|---|---|
| `smk37_map.h` | the flash map, the identities, the flash parts; given first to every compile of the SMK build (`-include`) |
| `fm1_lcd_hw.h` | the ST7789V on SPI1 group A (CS PC8, D/C PB5, no reset, no backlight pin), its tuned settings; the pad LEDs' frame on group B |
| `fm1_input.h` | the 16 x 8 matrix through four 74HC595 on SPI2 (37 keys with two contacts: velocity; 18 buttons; 8 encoders), the three analog muxes (16 pads, 4 faders, the wheels, pedal, battery), the 19 button LEDs |
| `fm1_adc.h` | the muxes' readings and the SMK's ADC timing; K1 as the MASTER pot |
| `fm1_uart.h` | no MIDI IN (PH8 is a matrix row) |

The audio is the FM-1's (`boards/fm1/fm1_audio.h`). The build puts this directory ahead of `chip/` and `boards/fm1`.

Status: Alpha, run in fm1-emu's SMK-37 target only, never on a unit by the author.

Facts (pins, registers, contact codes, timings, the flash map, the calibration record) from
[fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37), `docs/01-hardware.md` and `docs/02-update-and-recovery.md`
(CC BY-NC 4.0): facts only, none of its text or code, and no code of the GPL SMK-37 layers. Written for Fune, MPL-2.0.
