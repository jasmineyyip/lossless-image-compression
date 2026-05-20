#pragma once

// reversible YCoCg-R color transform
// RGB -> (Y, Co, Cg)
// Y  in [0, 255] (luma)
// Co in [-255, 255] (orange-blue chroma)
// Cg in [-255, 255] (green-magenta chroma)

inline void ycocg_r_forward(int R, int G, int B, int& Y, int& Co, int& Cg) {
    Co = R - B;
    int t = B + (Co >> 1);
    Cg = G - t;
    Y  = t + (Cg >> 1);
}

inline void ycocg_r_inverse(int Y, int Co, int Cg, int& R, int& G, int& B) {
    int t = Y - (Cg >> 1);
    G = Cg + t;
    B = t - (Co >> 1);
    R = B + Co;
}