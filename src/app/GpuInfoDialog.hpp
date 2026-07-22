#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QPlainTextEdit;

class GpuInfoDialog : public QDialog {
    Q_OBJECT
public:
    struct GlInfo {
        QString renderer, vendor, version;
    };

    explicit GpuInfoDialog(const GlInfo& info, QWidget* parent = nullptr);

private:
    void buildUi(const GlInfo& info);
    void applyAndRestart();

    QLineEdit*      m_env_edit{nullptr};
    QPlainTextEdit* m_info_view{nullptr};
};
