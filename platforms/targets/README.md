# Targets

One file a target: what it builds, its screen, its controls. `tools/kbuild.py` reads them (with the source groups in
`../platforms.toml`) for every build, and makes the profile table `src/ui/profiles_gen.h` from them:

```bash
python3 tools/kbuild.py --list          # the targets: status, screen, inputs
python3 tools/kbuild.py --profiles      # src/ui/profiles_gen.h again, after a change here (a test checks it is current)
python3 tools/kbuild.py fm1 defines     # what a shell build takes (sources, defines, cflags, profile)
```

A new device: a file here with `[screen]` and `[controls]`, its name in `[profiles] order` (`../platforms.toml`), then
`--profiles`. Before it has one: `kreisel --size WxH --input pad,keyboard` (the SDL build) tries any screen.

## Fields

| Field | What |
|---|---|
| `name` | what it is, in words |
| `status` | `target` (built and kept working), `fixture` (no device: tests, screenshots), `kept` (code kept, not maintained) |
| `profile` | the profile it draws with, when it has no `[screen]` of its own (Linux: `mac`) |
| `[build]` `groups`, `sources`, `cflags`, `[build.defines]` | what a shell build compiles (the FM-1, the Switch, the web); CMake takes the groups only |
| `[screen]` `size = [w, h]` | the logical size (the UI's units) |
| `[screen]` `pixels = [w, h]`, `diag` | a real screen instead: its pixels and diagonal (inches); the size is then in dp |
| `[screen]` `font` | `vector` (default) or `bitmap` (baked: no FPU) |
| `[controls]` `input` | any of `knobs`, `keys` (a music key bed), `pad`, `touch`, `keyboard`, `mouse`: it is given only these |
| `[controls]` `knobs`, `shown` | its knobs (encoders under the screen); the cards in a row |
| `[controls]` `keys` | its own keys' caps by role (ARM1..4, OK, CANCEL, SAVE), where it has them |
| `[layout]` `add`, `drop` | its own layout flags over what size and inputs give: `leds`, `preset_pop`, `connect` |

The layout itself follows from the size and the inputs (`k_profile_layout`, `src/ui/draw.c`): a device needs `[layout]` only
for what is its alone (the FM-1's LEDs).
