#include "util/ErrorReporter.hpp"
#include "util/Logger.hpp"

#include <cpl_error.h>

ErrorReporter& ErrorReporter::instance() {
    static ErrorReporter inst;
    return inst;
}

// GDAL error handler — routes CPLError()/driver faults (previously lost to stderr)
// into the central pathway. Signature must match CPLErrorHandler exactly.
static void CPL_STDCALL fvGdalErrorHandler(CPLErr eErrClass, CPLErrorNum /*no*/,
                                           const char* msg) {
    int level;
    switch (eErrClass) {
        case CE_None:    return;          // not an error — ignore
        case CE_Debug:   level = 1; break;
        case CE_Warning: level = 3; break;
        case CE_Failure: level = 4; break;
        case CE_Fatal:   level = 5; break;
        default:         level = 4; break;
    }
    ErrorReporter::instance().report(level, QStringLiteral("GDAL"),
                                     QString::fromUtf8(msg ? msg : ""));
}

void ErrorReporter::install() {
    CPLSetErrorHandler(&fvGdalErrorHandler);
    FV_INFO("ErrorReporter: GDAL error handler installed");
}

void ErrorReporter::report(int level, const QString& context,
                           const QString& message) {
    const QString full = context.isEmpty()
        ? message
        : QStringLiteral("[%1] %2").arg(context, message);
    const std::string s = full.toStdString();
    switch (level) {
        case 5:  FV_CRITICAL("{}", s); break;
        case 4:  FV_ERROR("{}", s);    break;
        case 3:  FV_WARN("{}", s);     break;
        case 2:  FV_INFO("{}", s);     break;
        default: FV_DEBUG("{}", s);    break;
    }
    emit reported(level, full);
}
