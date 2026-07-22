#pragma once
#include "core/Extent.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <numbers>

// Wraps the 6-element GDAL affine geotransform.
// GT[0] = top-left X,  GT[1] = pixel width,  GT[2] = row rotation
// GT[3] = top-left Y,  GT[4] = col rotation, GT[5] = pixel height (negative for N-up)
struct GeoTransform {
    double gt[6]{0, 1, 0, 0, 0, -1};

    glm::dvec2 pixelToGeo(double col, double row) const {
        return {
            gt[0] + (col + 0.5) * gt[1] + (row + 0.5) * gt[2],
            gt[3] + (col + 0.5) * gt[4] + (row + 0.5) * gt[5]
        };
    }

    glm::dvec2 geoToPixel(double x, double y) const {
        double det = gt[1] * gt[5] - gt[2] * gt[4];
        if (std::abs(det) < 1e-15) return {0, 0};
        double dx = x - gt[0];
        double dy = y - gt[3];
        return {
            (dx * gt[5] - dy * gt[2]) / det - 0.5,
            (dy * gt[1] - dx * gt[4]) / det - 0.5
        };
    }

    Extent extent(int width, int height) const {
        double tl_x = gt[0], tl_y = gt[3];
        double tr_x = gt[0] + width  * gt[1];
        double tr_y = gt[3] + width  * gt[4];
        double bl_x = gt[0]                  + height * gt[2];
        double bl_y = gt[3]                  + height * gt[5];
        double br_x = gt[0] + width  * gt[1] + height * gt[2];
        double br_y = gt[3] + width  * gt[4] + height * gt[5];
        return {
            std::min({tl_x, tr_x, bl_x, br_x}),
            std::min({tl_y, tr_y, bl_y, br_y}),
            std::max({tl_x, tr_x, bl_x, br_x}),
            std::max({tl_y, tr_y, bl_y, br_y})
        };
    }

    bool isNorthUp() const { return std::abs(gt[2]) < 1e-9 && std::abs(gt[4]) < 1e-9; }

    double rotationAngleDeg() const {
        return std::atan2(gt[2], gt[1]) * (180.0 / std::numbers::pi);
    }
};
