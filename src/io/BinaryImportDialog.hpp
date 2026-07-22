#pragma once
#include <QDialog>
#include "io/BinaryRasterParser.hpp"

class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QTextEdit;

class BinaryImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit BinaryImportDialog(const QString& file_path, QWidget* parent = nullptr);

    BinaryRasterSpec spec() const;

private slots:
    void validate();
    void updateHexPreview();

private:
    void setupUi(const QString& file_path);

    QString      m_file_path;
    QSpinBox*    m_lines_spin{nullptr};
    QSpinBox*    m_samples_spin{nullptr};
    QSpinBox*    m_bands_spin{nullptr};
    QComboBox*   m_dtype_combo{nullptr};
    QComboBox*   m_interleave_combo{nullptr};
    QSpinBox*    m_header_spin{nullptr};
    QCheckBox*   m_big_endian_chk{nullptr};
    QTextEdit*   m_hex_preview{nullptr};
};
