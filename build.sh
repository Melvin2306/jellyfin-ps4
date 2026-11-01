#!/bin/bash
# Builds the PS4 package inside the toolchain container (tools/docker).
#   ./build.sh          -> build_ps4/jellyfin-ps4-<version>.pkg
# First run also builds FFmpeg and HarfBuzz (cached in ffmpeg_ps4/, harfbuzz_ps4/).
set -euo pipefail
cd "$(dirname "$0")"
if [ "${1:-}" = "--inside" ]; then
	export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
	[ -f ffmpeg_ps4/install/lib/libavcodec.a ] || tools/build_ffmpeg_ps4.sh
	[ -f harfbuzz_ps4/install/lib/libharfbuzz.a ] || tools/build_harfbuzz_ps4.sh
	make -f Makefile.ps4 -j"$(nproc)"
	echo "Package: build_ps4/jellyfin-ps4-$(cat VERSION).pkg"
	exit 0
fi
docker build --platform linux/amd64 -t jellyfin-ps4-build tools/docker
docker run --rm --platform linux/amd64 -v "$PWD":/src jellyfin-ps4-build ./build.sh --inside
