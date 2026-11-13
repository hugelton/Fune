#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build Fune for the M-VAVE SMK-37 Pro (Alpha): the FM-1's build (platforms/fm1hw/build.sh) on the SMK's
# board. build/smk37/kreisel.fwsc (identity "SMK-37 Pro_913", 36 interleave blocks).
#
#   platforms/smk37/build.sh                          the SMK-37 Pro
#   KREISEL_SMK_MODEL=elite platforms/smk37/build.sh  the SMK-37 Elite (build/smk37_elite)
#   KREISEL_FM1_BOOTCHECK=0 ...                       for fm1-emu only (it boots the package's own boot file): never to hand out
#
# The other settings as build.sh has them. Never flashes anything.
set -e
KREISEL_BOARD=smk37 exec sh "$(cd "$(dirname "$0")" && pwd)/../fm1hw/build.sh" "$@"
