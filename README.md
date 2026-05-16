# Lossless Image Compression

Building a lossless image codec in C++ to figure out how lossless compression actually works under the hood. Plan is to start with prediction and entropy coding on grayscale, add context modeling, then color, then maybe video later.

## Set up

You'll need a C++17 compiler, CMake, and Python (just for the histogram plot).

```
./fetch_deps.sh        # downloads stb_image.h
cmake -B build
cmake --build build
pip install matplotlib
```

## What works so far

`entropy_gap.cpp` runs Paeth prediction on a grayscale image and compares the Shannon entropy of the raw pixels against the residuals. The difference between those two numbers is roughly the compression headroom you'd get with a perfect entropy coder over those residuals.

```
./build/entropy_gap kodim01.png
python plot_histograms.py
```

Any image works. Kodak test images (kodim01 through kodim24) are what people usually benchmark against, so I'm using those.

## Next steps

- range coder over the residuals so this actually produces a compressed file, and compare its size to PNG
- context modeling with adaptive probability tables (LOCO-I style)
- color images via YCoCg-R
- proper file format with a header / magic bytes / version
- benchmark against PNG, WebP-lossless, JPEG-LS
- video, eventually

## References

- Sayood, *Introduction to Data Compression*
- Weinberger, Seroussi, Sapiro — the LOCO-I / JPEG-LS paper
- Witten/Neal/Cleary 1987 - "Arithmetic Coding for Data Compression" 
- PNG filter spec
- stb_image.h
