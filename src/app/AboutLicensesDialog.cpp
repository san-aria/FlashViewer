#include "app/AboutLicensesDialog.hpp"

#include <QVBoxLayout>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QFile>

QString AboutLicensesDialog::licenseManifest() {
    QFile f(QString::fromLatin1(kResourcePath));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

AboutLicensesDialog::AboutLicensesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Licenses"));
    setMinimumSize(620, 520);

    auto* lay = new QVBoxLayout(this);

    auto* view = new QTextBrowser(this);
    view->setOpenExternalLinks(true);
    const QString manifest = licenseManifest();
    if (manifest.isEmpty())
        view->setPlainText(tr("Third-party license manifest is unavailable."));
    else
        view->setMarkdown(manifest);
    lay->addWidget(view);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(btns);
}
