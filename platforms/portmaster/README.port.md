# Fune (test build)

A groovebox. This is a test build for PortMaster on arm64 handhelds (H700: Anbernic RG35XX, RG40XX, RG CubeXX ...).

- Copy `Fune.sh` and the `fune` folder into `ports/`, start Fune from Ports.
- The screen is the H700s' 640 x 480, whole: the profile h700, 320 x 240 drawn at 2 x.
- Songs, presets and plugins are kept in `fune/conf/kreisel/`. Two example plugins come with it (FM2, FLAT).
- Exit: Select + Start. A log of the last run: `fune/log.txt`.
- Needs glibc 2.34 or newer (recent muOS, Knulli, ROCKNIX), SDL2 and ALSA (the firmware has them).
