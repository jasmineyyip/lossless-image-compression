# lossless-image-compression

A lossless image codec written from scratch in C++. Compresses 8-bit grayscale images via Paeth prediction and a range coder, reaching within 0.02% of Shannon entropy on natural photos.

[Live demo](https://lossless-image-compression.vercel.app/) — drop any image in, see it compressed in your browser via WebAssembly.

## Usage

Two command-line tools, `compress` and `decompress`, that round-trip a PNG through a custom `.bin` format:

```
./build/compress kodim01.png kodim01.bin    # 273,629 bytes (5.567 bpp)
./build/decompress kodim01.bin kodim01.png  # verifies the decoded image matches the original exactly
```

The codec has two stages. First, a Paeth predictor (the same one PNG uses as filter type 4) replaces each pixel with its prediction error from three already-decoded neighbors — adjacent pixels in natural images are similar, so the residuals cluster tightly near zero. Then a bit-level range coder encodes those residuals against a single global probability model, hitting within hundredths of a bit per pixel of the Shannon limit on real photos.

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

## Status

Currently working: 8-bit grayscale, single global probability model, WASM build for the browser, round-trip verified on the Kodak image set.

Open: context-adaptive probability tables (~20% bpp improvement expected), color support via YCoCg-R, replacing the linear-scan symbol lookup with binary search.

## References

- Witten, Neal, Cleary (1987), "Arithmetic coding for data compression"
- Weinberger, Seroussi, Sapiro (2000), the LOCO-I / JPEG-LS paper
- Sayood, *Introduction to Data Compression*
- PNG specification, filter chapter