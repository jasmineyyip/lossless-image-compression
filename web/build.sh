#!/usr/bin/env bash
set -e
em++ codec.cpp -o public/codec.js \
    -I .. -I ../deps \
    -s EXPORTED_FUNCTIONS='["_compress_image","_decompress_image","_free_buffer","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAP32"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -O2
echo "Built public/codec.js and public/codec.wasm"