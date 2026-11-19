# Licensing

Fune is open source under the **Mozilla Public License 2.0** (`LICENSE`), with some parts under other licences so each can
be used the way it is meant to be. Every source file names its licence in an `SPDX-License-Identifier` line; where a folder
differs from the whole, it has its own `LICENSE`.

| Part | Licence | Where |
| --- | --- | --- |
| Fune itself: the core, the UI, the engines, the platforms, the tools, the tests | MPL-2.0 | `src/`, `platforms/`, `tools/`, `tests/`, `CMakeLists.txt` |
| The plugin SDK and the example plugins: so plugins can be published under any licence | MIT | `sdk/`, `plugins/` |
| The documentation | CC BY 4.0 | `docs/` (see `docs/LICENSE`) |
| felucca-bsp: the FM-1's hardware layer (with code by other authors) | MPL-2.0 | `third_party/felucca-bsp/` (its `NOTICE`, `LICENSING.md`) |
| pendel-dsp: the effects' DSP | MPL-2.0 | `third_party/pendel-dsp/` |
| Ableton Link 4.1.1 (tempo, beat and start/stop sync; the SDL builds) | GPL-2.0-or-later | `third_party/link/` (its `LICENSE.md`) |
| asio, bundled with Link | BSL-1.0 | `third_party/link/modules/asio-standalone/` |
| Fukiai icons | MIT | `assets/icons/`, `assets/fonts/MIT-Fukiai.txt` |
| Inter Tight, Press Start 2P fonts | SIL OFL 1.1 | `assets/fonts/OFL-*.txt` |
| Drum samples (made from Virtuosity Drums) | CC0 1.0 | `assets/drums/` |

MPL-2.0 is a file-level licence: a larger work (a port, a plugin, an app) may use Fune's files under any terms of its own,
as long as Fune's files, changed or not, stay under the MPL and their source is made available.

Ableton Link is under the GPL, version 2 or later. The SDL builds (macOS, Linux, the Cardputer Zero, PortMaster) are built
with it unless CMake's `KREISEL_LINK` is turned off, so each of those programs, as a whole, is distributed under the GPL
(MPL-2.0 section 3.3 allows it); Fune's own files stay MPL-2.0, and their source is this repository with `third_party/link`.
The FM-1 and SMK-37 firmware and the web build have no Link and are not affected. `tools/make_notices.py` puts the GPL, Link's
notice and asio's into those packages. A plugin loaded by a build with Link should be under a GPL-compatible licence when it
is handed out with it; the SDK itself stays MIT.

The FM-1 firmware package also carries three boot files of the JieLi AC79 SDK unchanged (Apache-2.0) and is linked with the
toolchain's C library; see the notices `tools/make_notices.py` writes into each package.

The names "Fune" and "Hügelton Instruments" and the anchor logo are not licensed by any of the above: see `TRADEMARK.md`.
