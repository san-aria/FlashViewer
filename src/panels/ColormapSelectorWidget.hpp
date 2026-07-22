#pragma once
#include <QWidget>
#include <QComboBox>
#include <QCheckBox>

class RasterLayer;

class ColormapSelectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColormapSelectorWidget(QWidget* parent = nullptr);
    void setLayer(RasterLayer* layer);

signals:
    void colormapChanged();

private slots:
    void onIndexChanged(int idx);
    void onInvertToggled(bool checked);

private:
    RasterLayer* m_layer{nullptr};
    QComboBox*   m_combo{nullptr};
    QCheckBox*   m_invert_cb{nullptr};
    bool         m_updating{false};
};
