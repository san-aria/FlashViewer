#pragma once
#include <QWidget>
#include <QLabel>
#include <QColor>
#include <vector>
#include <memory>

class RasterLayer;
class LayerManager;
class QDoubleSpinBox;
class QRadioButton;
class QStackedWidget;
class QScrollArea;

// Custom widget that draws a histogram with two draggable stretch handles.
class HistogramView : public QWidget {
    Q_OBJECT
public:
    explicit HistogramView(QWidget* parent = nullptr);

    // Bin over [data_min, data_max] (full data range); default the visible window to
    // [view_min, view_max] (typically the 1/99 range). The window auto-expands to
    // include the stretch handles.
    void setData(const std::vector<float>& samples, float data_min, float data_max,
                 float view_min, float view_max);
    void setStretch(float lo, float hi);
    void clear();
    // Bin fill colour (alpha is forced to match the default gray-mode translucency).
    // An invalid colour falls back to the palette highlight (default behaviour).
    void setBarColor(const QColor& c) { m_bar_color = c; update(); }
    float stretchMin() const { return m_lo; }
    float stretchMax() const { return m_hi; }

signals:
    void stretchChanged(float lo, float hi);
    void stretchDragging(float lo, float hi);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void changeEvent(QEvent* e) override;
    QSize sizeHint() const override { return {280, 150}; }
    QSize minimumSizeHint() const override { return {180, 110}; }

private:
    float toNorm(float v) const;       // data value → [0,1] in display range
    float fromNorm(float n) const;     // [0,1] → data value in display range
    int   normToX(float n) const;      // [0,1] → widget pixel x
    float xToNorm(int px) const;       // widget pixel x → [0,1]
    void  updateDisplayRange();        // recompute m_disp_min/max from data + stretch

    std::vector<unsigned int> m_hist;  // 256-bin histogram counts (over full data range)
    float m_data_min{0}, m_data_max{1}; // full data range (binning domain)
    float m_view_min{0}, m_view_max{1}; // default visible window (e.g. 1/99 range)
    float m_disp_min{0}, m_disp_max{1}; // display range: encompasses view AND stretch
    float m_lo{0}, m_hi{1};
    QColor m_bar_color{};               // invalid → palette highlight
    int   m_drag{0};   // 0=none, 1=lo handle, 2=hi handle
    static constexpr int kHandleW = 6;
    static constexpr int kPadX = 10, kPadY = 6;
};

// One band's histogram with value/percentile clip controls and an Auto-Stretch button.
// Used both for the single pseudocolor (Gray) band and for each R/G/B channel in
// composite mode (FR-HST-6). `channel` is -1 for gray (drives the layer's gray
// stretch) or 0/1/2 for R/G/B (drives that channel's stretch).
class BandHistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit BandHistogramWidget(QWidget* parent = nullptr);
    void setBarColor(const QColor& c);
    void setTitle(const QString& t);
    void load(RasterLayer* layer, int band1based, int channel);
    void clear();

signals:
    void stretchChanged();   // a stretch edit was applied (repaint the canvas)

private slots:
    void onStretchViewChanged(float lo, float hi);
    void onStretchDragging(float lo, float hi);
    void onSpinChanged();
    void onPctChanged();
    void onModeChanged();
    void onAutoStretchClicked();

private:
    void  sample();
    void  applyStretch(float lo, float hi);
    void  fillValueSpins(float lo, float hi);
    void  fillPctSpins(float lo, float hi);
    float curLo() const;
    float curHi() const;
    void  writeStretch(float lo, float hi);

    RasterLayer*     m_layer{nullptr};
    int              m_band{1};       // 1-based dataset band
    int              m_channel{-1};   // -1 gray, 0/1/2 RGB
    HistogramView*   m_view{nullptr};
    QLabel*          m_title{nullptr};
    QLabel*          m_stats_label{nullptr};
    QDoubleSpinBox*  m_lo_spin{nullptr};
    QDoubleSpinBox*  m_hi_spin{nullptr};
    QDoubleSpinBox*  m_lo_pct{nullptr};
    QDoubleSpinBox*  m_hi_pct{nullptr};
    QRadioButton*    m_mode_values{nullptr};
    QRadioButton*    m_mode_pct{nullptr};
    QStackedWidget*  m_ctrl_stack{nullptr};
    std::vector<float> m_sorted;
    bool             m_updating{false};
};

// Dock panel: one histogram (pseudocolor) or three (RGB composite), in a scroll area.
class HistogramPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistogramPanel(QWidget* parent = nullptr);
    void setLayerManager(LayerManager* mgr);
    // Phase 18 #8: >1 layer selected in the Layers panel ⇒ no single subject. While
    // suppressed the panel shows its empty state and ignores LayerManager refreshes;
    // clearing it restores the view from the active layer.
    void setSuppressed(bool on);

signals:
    void stretchChanged(RasterLayer* layer, float lo, float hi);

private slots:
    void onActiveLayerChanged(int index);

private:
    void configureFor(RasterLayer* layer);

    LayerManager*        m_mgr{nullptr};
    RasterLayer*         m_layer{nullptr};
    bool                 m_suppressed{false};
    BandHistogramWidget* m_bands[3]{nullptr, nullptr, nullptr};
    QLabel*              m_empty_label{nullptr};
    // The two inter-band rules, held directly. They must NOT be re-discovered by
    // scanning the layout for QFrame children: QLabel derives from QFrame, so such a
    // scan also matches m_empty_label and toggles "No layer" as if it were a rule.
    QFrame*              m_separators[2]{nullptr, nullptr};
};
