#pragma once
#include <string>
#include <vector>

class RasterLayer;

// Read every band of `rl` at one geographic point — the single implementation shared by the
// Pixel Inspector table and the Spectral Plot (Phase 26), so the number a user reads in the
// table is exactly the number the curve is drawn from.
//
// (geo_x, geo_y) are expressed in `geoWkt` — the clicked pane's Project CRS. The point is
// transformed geoWkt → the layer's SOURCE CRS before sampling, so analysis reads the correct
// source pixel under on-the-fly reprojection (FR-CRS-4). An empty `geoWkt` means geographic.
//
// Returns false (leaving `out` empty) when the layer has no dataset, the point falls outside
// its grid, or the read fails. No-data samples come back as quiet NaN so callers can render
// them as gaps rather than spikes.
bool fvSamplePixelBands(RasterLayer* rl, double geo_x, double geo_y,
                        const std::string& geoWkt, std::vector<double>& out);
