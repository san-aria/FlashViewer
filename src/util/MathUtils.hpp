#pragma once
#include <vector>
#include <algorithm>
#include <cmath>

namespace MathUtils {

inline float median(std::vector<float> v) {
    if (v.empty()) return 0.f;
    size_t n = v.size();
    std::nth_element(v.begin(), v.begin() + n/2, v.end());
    float m = v[n/2];
    if (n % 2 == 0) {
        std::nth_element(v.begin(), v.begin() + n/2 - 1, v.end());
        m = (m + v[n/2 - 1]) / 2.f;
    }
    return m;
}

inline float quantile(std::vector<float> v, float p) {
    if (v.empty()) return 0.f;
    p = std::clamp(p, 0.f, 1.f);
    size_t idx = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

inline float mean(const std::vector<float>& v) {
    if (v.empty()) return 0.f;
    float s = 0.f;
    for (float x : v) s += x;
    return s / v.size();
}

inline float stddev(const std::vector<float>& v) {
    if (v.size() < 2) return 0.f;
    float m = mean(v);
    float s = 0.f;
    for (float x : v) { float d = x - m; s += d*d; }
    return std::sqrt(s / (v.size() - 1));
}

} // namespace MathUtils
