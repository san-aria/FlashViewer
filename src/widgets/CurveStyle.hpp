#pragma once
// How a plotted curve is coloured and stroked so it names the PANE its layer lives in
// (Phase 26.2). Free + inline, needing only QColor, so the rule is unit-testable without a
// chart — the fvPaneColor / fvPaneColorsClash precedent in render/Pane.hpp.
//
// Hue carries the pane; a lightness step plus a dash pattern carry the layer within it.
//
// Hue deliberately does NOT vary within a pane. The pane palette's own hues sit as little as
// 14° apart (dark: #f28482 at 1° vs #ff8fab at 347°), so a hue spread wide enough to be seen
// is wide enough to read as a DIFFERENT pane. Lightness at fixed hue cannot make that mistake.

#include <QColor>
#include <Qt>
#include <algorithm>
#include <cmath>

/// Lightness steps before the ramp repeats and the dash pattern advances.
inline constexpr int kFvCurveShadeSteps = 4;

/// Dash patterns before the whole cycle repeats. 4 shades × 4 dashes = 16 distinct
/// appearances within one pane, which is well past the point where a legend is doing the
/// work anyway.
inline constexpr int kFvCurveDashCount = 4;

/// Lightness band the ramp is confined to, per theme. Outside it a curve either blurs into
/// the plot background (bright on light / dark on dark) or goes too dark to read.
inline constexpr double kFvCurveLightMin  = 0.20, kFvCurveLightMax  = 0.68;   // light theme
inline constexpr double kFvCurveDarkMin   = 0.40, kFvCurveDarkMax   = 0.88;   // dark theme

/// Colour for the `indexInPane`-th curve belonging to a pane wearing `paneColor`.
///
/// Index 0 returns `paneColor` **verbatim** — so the common case (one left-click, one curve
/// per pane) is exactly the pane's colour, with no approximation. Later indices keep the hue
/// and saturation and walk the lightness band in equal steps, wrapping inside the band, so
/// every step is `band / kFvCurveShadeSteps` apart however light or dark the pane colour is.
/// `paneColor` must be valid; callers resolve an uncoloured pane to the palette highlight.
inline QColor fvCurveColor(const QColor& paneColor, int indexInPane, bool dark) {
    if (!paneColor.isValid()) return QColor(Qt::gray);
    const int n = kFvCurveShadeSteps;
    const int shade = ((indexInPane % n) + n) % n;
    if (shade == 0) return paneColor;

    const double lo   = dark ? kFvCurveDarkMin : kFvCurveLightMin;
    const double hi   = dark ? kFvCurveDarkMax : kFvCurveLightMax;
    const double band = hi - lo;

    // Qt 6's getHslF/fromHslF take float, not qreal — keep the whole computation in float so
    // there is no lossy round-trip and no overload ambiguity.
    float h = 0.0f, s = 0.0f, l = 0.0f, a = 0.0f;
    paneColor.getHslF(&h, &s, &l, &a);
    // A pane colour outside the band still anchors the ramp, clamped in — index 0 already
    // returned the true colour, so nothing here has to reproduce it exactly.
    const float base = std::clamp(static_cast<float>(l), static_cast<float>(lo),
                                  static_cast<float>(hi));
    // Wrap INSIDE the band rather than clamping at its edges: clamping would pile two or
    // three shades onto the same lightness whenever the pane colour sits near an edge.
    const float off = std::fmod(static_cast<float>((base - lo) + shade * (band / n)),
                                static_cast<float>(band));
    // A greyscale pane colour has an undefined hue (h < 0 from Qt); saturate nothing, the
    // lightness ramp alone still separates the curves.
    return QColor::fromHslF(h < 0.0f ? 0.0f : h, h < 0.0f ? 0.0f : s,
                            static_cast<float>(lo) + off, a);
}

/// Dash pattern for the `indexInPane`-th curve. Advances only once the lightness ramp has
/// been used up, so the two channels are independent: shade is the fast axis, dash the slow
/// one. Also the reason colour is never the ONLY channel (FR-A11Y-6).
inline Qt::PenStyle fvCurveDash(int indexInPane) {
    static const Qt::PenStyle kDashes[kFvCurveDashCount] = {
        Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine,
    };
    const int n = kFvCurveShadeSteps * kFvCurveDashCount;
    const int i = ((indexInPane % n) + n) % n;
    return kDashes[i / kFvCurveShadeSteps];
}
