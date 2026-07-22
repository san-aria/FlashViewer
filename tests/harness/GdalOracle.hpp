#pragma once
// GdalOracle — independent reference values computed by calling GDAL directly,
// used to check application output (statistics, NDVI, etc.) within a tolerance.
// Deliberately does NOT go through RasterDataset, so it is a true oracle.

#include <string>

namespace GdalOracle {

struct Stats { double min{0}, max{0}, mean{0}, stddev{0}; bool ok{false}; };

// GDAL ComputeStatistics(approx=FALSE) for a 1-based band.
Stats bandStats(const std::string& path, int band_1based);

// Read a single pixel of a 1-based band as double (no-data not masked).
bool readPixel(const std::string& path, int band_1based, int col, int row,
               double& out);

} // namespace GdalOracle
