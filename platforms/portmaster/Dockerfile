# The build machine for PortMaster (the H700 handhelds and their kin): arm64 Debian, SDL2 and ALSA's headers, the generators'
# Python (3.11: tools/kbuild.py's tomllib; Pillow: the fonts). (the binary wants glibc 2.34 or newer: a recent firmware)
FROM --platform=linux/arm64 debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc libc6-dev make cmake pkg-config python3 python3-pil python3-fonttools libsdl2-dev libasound2-dev file \
    && rm -rf /var/lib/apt/lists/*
