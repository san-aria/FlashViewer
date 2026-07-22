#include "GdalOracle.hpp"

#include <gdal_priv.h>

namespace GdalOracle {

Stats bandStats(const std::string& path, int band_1based) {
    GDALAllRegister();
    Stats s;
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds) return s;
    GDALRasterBand* band = ds->GetRasterBand(band_1based);
    if (band &&
        band->ComputeStatistics(FALSE, &s.min, &s.max, &s.mean, &s.stddev,
                                nullptr, nullptr) == CE_None) {
        s.ok = true;
    }
    GDALClose(ds);
    return s;
}

bool readPixel(const std::string& path, int band_1based, int col, int row,
               double& out) {
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds) return false;
    GDALRasterBand* band = ds->GetRasterBand(band_1based);
    bool ok = false;
    if (band) {
        float v = 0.0f;
        if (band->RasterIO(GF_Read, col, row, 1, 1, &v, 1, 1, GDT_Float32, 0, 0)
            == CE_None) {
            out = static_cast<double>(v);
            ok = true;
        }
    }
    GDALClose(ds);
    return ok;
}

} // namespace GdalOracle
