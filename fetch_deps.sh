#!/usr/bin/env bash
# Fetch external single-header dependencies into deps/.
set -euo pipefail

mkdir -p deps
curl -fsSL -o deps/stb_image.h \
    https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

echo "Fetched stb_image.h into deps/"
