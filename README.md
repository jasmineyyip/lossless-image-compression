# lossless-image-compression

A lossless image codec written in C++ from scratch. It compresses 8-bit grayscale and 24-bit RGB images using YCoCg-R decorrelation, Paeth prediction, and a range coder, reaching within hundredths of a bit per channel-pixel of the Shannon entropy of the prediction residuals.

Check out this [live demo](https://lossless-image-compression.vercel.app/) compress any images yourself!

## Usage

Two command-line tools, `compress` and `decompress`:

```
./build/compress kodim01.png kodim01.bin    # 543,114 bytes (3.60 bits/channel-pixel, 2.17x raw)
./build/decompress kodim01.bin kodim01.png  # verifies the decoded image matches the original byte-for-byte
```

Handles both grayscale and color PNGs automatically based on the source.

The codec has three (3) stages. 
1. For color images, a reversible YCoCg-R transform decorrelates RGB into one luma channel and two chroma channels. Chroma residuals on natural photos are dramatically more predictable than raw R, G, B.
2. A Paeth predictor that replaces each pixel with its prediction error from three decoded neighbors.
3. A bit-level range coder which encodes those residuals against a probability model built from the histogram, hitting within hundredths of a bit per channel-pixel of the Shannon limit on real photos.

The `web/` directory contains the same codec compiled to WebAssembly with a small React UI on top.

## Build

C++17 compiler and CMake:

```
./fetch_deps.sh
cmake -B build
cmake --build build
```

The web demo additionally needs Emscripten and Node:

```
cd web
./build.sh
npm install
npm run dev
```

## Project Status

Currently working: grayscale and 24-bit color, YCoCg-R for color decorrelation, single global probability model per channel, WASM build for the browser, round-trip verified on the Kodak image set.

Open: context-adaptive probability tables (expecting a ~20% bpp improvement expected), replacing the linear-scan symbol lookup with binary search, larger predictor family (MED, GAP).

## References

- Witten, Neal, Cleary (1987), "Arithmetic coding for data compression"
- Weinberger, Seroussi, Sapiro (2000), the LOCO-I / JPEG-LS paper
- Malvar, Sullivan (2003), "YCoCg-R: A color space with RGB reversibility and low dynamic range"
- PNG specification, filter chapter
