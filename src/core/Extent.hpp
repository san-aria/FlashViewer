#pragma once
#include <algorithm>
#include <glm/glm.hpp>

struct Extent {
    double xmin{0}, ymin{0}, xmax{0}, ymax{0};

    bool isValid()    const { return xmax > xmin && ymax > ymin; }
    double width()    const { return xmax - xmin; }
    double height()   const { return ymax - ymin; }
    glm::dvec2 center() const { return {(xmin + xmax) * 0.5, (ymin + ymax) * 0.5}; }

    bool contains(double x, double y) const {
        return x >= xmin && x <= xmax && y >= ymin && y <= ymax;
    }

    bool overlaps(const Extent& o) const {
        return !(o.xmin > xmax || o.xmax < xmin ||
                 o.ymin > ymax || o.ymax < ymin);
    }

    Extent united(const Extent& o) const {
        return {std::min(xmin, o.xmin), std::min(ymin, o.ymin),
                std::max(xmax, o.xmax), std::max(ymax, o.ymax)};
    }

    static Extent invalid() { return {}; }
};
