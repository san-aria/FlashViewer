#pragma once
// Shared warp-resampling policy (Phase 9, FR-OPS-2/5, FR-ACC-3). Header-only + free so the
// GDAL Operations dialog (Warp tab selector) and the tests (TC-OPS-05/06/07) use the exact
// same option set and data-aware default, without pulling in a QWidget. GDAL tokens (`-r`
// values) are the authoritative form used throughout the codebase (see RasterDataset::warpToGrid
// and RasterMathDialog::fillResampling).
#include <gdal.h>   // GDALDataType, GDALDataTypeIsInteger
#include <cstddef>
#include <cstring>

// One selectable resampling method: a human label and its GDAL `-r` token.
struct FvResampleOption {
    const char* label;
    const char* token;
};

// The methods offered by the Warp tab, in display order. FR-OPS-5 requires at least
// {nearest, bilinear, cubic}; Lanczos is offered as a higher-quality extra (TC-OPS-05
// checks that >= 3 methods incl. near/bilinear/cubic are present).
inline constexpr FvResampleOption kFvResampleOptions[] = {
    { "Nearest",  "near" },
    { "Bilinear", "bilinear" },
    { "Bicubic",  "cubic" },
    { "Lanczos",  "lanczos" },
};
inline constexpr std::size_t kFvResampleOptionCount =
    sizeof(kFvResampleOptions) / sizeof(kFvResampleOptions[0]);

// Data-aware default (FR-OPS-5): integer/categorical rasters (and any palette/colortable
// raster) default to nearest so class values are preserved exactly (FR-ACC-3); continuous
// (float) rasters default to bilinear. The result is a GDAL `-r` token; the user may override.
inline const char* fvDefaultResampling(GDALDataType dt, bool hasColorTable) {
    if (hasColorTable || GDALDataTypeIsInteger(dt))
        return "near";
    return "bilinear";
}

// True when `token` names one of the offered methods (used to validate an inherited default).
inline bool fvIsKnownResampling(const char* token) {
    if (!token) return false;
    for (std::size_t i = 0; i < kFvResampleOptionCount; ++i)
        if (std::strcmp(token, kFvResampleOptions[i].token) == 0) return true;
    return false;
}
