#pragma once
// Managed-temp-file reaper (GDAL Ops / Raster Math "temporary output"). Header-only + free
// so the MainWindow layer-removal handler and the tests share one deletion path. A result
// raster written to QDir::tempPath() and tagged RasterLayer::ownsTempFile(true) is deleted
// here once its layer is removed and the GDAL handle is closed (see MainWindow reaper).
#include <QFile>
#include <QFileInfo>
#include <QString>

// Delete a managed temp file. Returns true when the file is gone afterwards (removed now, or
// already absent). A non-existent path is treated as success (idempotent). Empty paths are a
// no-op success. Never throws.
inline bool fvRemoveTempFile(const QString& path) {
    if (path.isEmpty()) return true;
    if (!QFileInfo::exists(path)) return true;
    return QFile::remove(path);
}
