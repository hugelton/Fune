Fune for the M5Stack Cardputer Zero -- Alpha
==============================================

A groovebox: 4 tracks of synths and drums, a step sequencer, effects.

ALPHA: built for the Zero's Linux (arm64, SDL2, ALSA) but NOT YET TRIED ON A DEVICE.
It has run only in M5Stack's Cardputer Zero emulator. Expect it to fail; the log
(~/.local/share/kreisel/log.txt) tells why.

Install (on the Zero, in a terminal or over SSH)
  tar xzf fune-zero-alpha-*-linux-arm64.tar.gz
  cd fune-zero-alpha-*-linux-arm64
  ./install.sh                 (or ./install.sh --launcher: also an entry in M5Stack's launcher, asks for sudo)
  fune                       (if ~/.local/bin is not on PATH: ~/.local/share/kreisel/app/fune)

  Needs SDL2 and ALSA's library (Raspberry Pi OS has them; else: sudo apt install libsdl2-2.0-0 libasound2).
  Remove: ./install.sh --remove (songs and presets stay).

Files
  ~/.local/share/kreisel/        songs, presets, settings, plugins (kept across updates)
  ~/.local/share/kreisel/app/    the program
  ~/.local/share/kreisel/log.txt the last run's log

Keys
  F Z X C          cursor up, left, down, right (the orange arrows, no Fn needed)
  K L              value up / down
  1 .. 0           pages: 1 HOME (mixer), 2 SEQ, 3 GLO, 4 SYNTH, 5 EDIT, 6 ENV, 7 LFO, 8 FX, 9 ARP, 0 every page
                   (the same key again: the next page of its group)
  OK / Del         yes / no
  Space            play / stop
  ESC, HOME        back
  , .              ALGORITHM: the pattern on SEQ, the engine, the effect, the settings' tabs
  r REC   s SAVE   g settings   u undo   t tools   o p presets   [ ] octave
  SAVE stops playback first ("Stop to save"); the song saved last opens at the next start.

Sound: the Zero's speaker / headphones through ALSA (SDL picks the device).
MIDI in and out: the ALSA sequencer (a USB MIDI keyboard on the USB host port); an output port "Fune".

Open source: MPL-2.0 (LICENSE.txt, LICENSING.md); the source at https://github.com/hugelton/Fune. Others' parts and
their licences: THIRD_PARTY_NOTICES.txt.
