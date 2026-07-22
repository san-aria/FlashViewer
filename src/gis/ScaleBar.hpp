#pragma once
#include <QWidget>
#include "render/Camera.hpp"

class ScaleBar : public QWidget {
    Q_OBJECT
public:
    explicit ScaleBar(QWidget* parent = nullptr);

    void update(const Camera& camera, bool is_geographic_crs = true);

protected:
    void paintEvent(QPaintEvent*) override;
    QSize sizeHint() const override { return {width(), 40}; }

private:
    double niceDistance(double units_per_px, int maxPx) const;

    double m_scale_px{100.0};
    double m_scale_value{1.0};
    QString m_scale_label;
    bool   m_valid{false};
};
