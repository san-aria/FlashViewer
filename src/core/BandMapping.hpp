#pragma once

// Describes which raster bands are mapped to display channels.
// For RGB: red_idx, green_idx, blue_idx are 1-based band numbers.
// For single-band pseudocolor: red_idx == green_idx == blue_idx.
struct BandMapping {
    int red_idx{1};    // 1-based band index -> R channel (or single-band)
    int green_idx{1};  // 1-based band index -> G channel
    int blue_idx{1};   // 1-based band index -> B channel

    bool isGrayscale() const {
        return red_idx == green_idx && green_idx == blue_idx;
    }

    int grayBand() const { return red_idx; }

    static BandMapping rgb(int r, int g, int b) {
        return {r, g, b};
    }
    static BandMapping gray(int band) {
        return {band, band, band};
    }
};
