# Fonts

| File | What | Licence |
| --- | --- | --- |
| `InterTight-VF.ttf` | Inter Tight, variable weight (copied from Felucca `assets/fonts`; renamed) | SIL OFL 1.1, `OFL-InterTight.txt` |
| `PressStart2P-Regular.ttf` | Press Start 2P (from Google Fonts' repository, `ofl/pressstart2p`); baked to 8 x 8 bitmaps in `src/ui/pixfont8.c` by `tools/gen_pix8.py` for the game-screen pages (LOFI) | SIL OFL 1.1, `OFL-PressStart2P.txt` (Reserved Font Name "Press Start 2P") |
| `fukiai.ttf` | Fukiai icon font, (c) Hügelton Instruments (copied from Felucca `web/`) | MIT, `MIT-Fukiai.txt` |

`fukiai.ttf` is not used yet: the font is being redrawn, and the UI draws placeholder icons (`g_icon`, `src/ui/gfx.c`) until it is ready.
