#include "io/CloudReader.hpp"
#include "io/RasterDataset.hpp"
#include "io/UrlGuard.hpp"
#include "util/Logger.hpp"
#include "util/ErrorReporter.hpp"

#include <cpl_http.h>
#include <gdal_priv.h>

void CloudReader::configureGdal() {
    CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
    CPLSetConfigOption("CPL_VSIL_CURL_CACHE_SIZE", "128000000");
    CPLSetConfigOption("CPL_VSIL_CURL_CHUNK_SIZE", "524288");

    // Remote-fetch hardening (FR-SEC-1/3, FR-ERR-2). Keep TLS verification ON — do
    // NOT set GDAL_HTTP_UNSAFESSL. Bound redirects, retries and timeouts so a
    // hostile/slow endpoint cannot hang or loop the fetch.
    CPLSetConfigOption("GDAL_HTTP_MAX_RETRY",   "2");
    CPLSetConfigOption("GDAL_HTTP_RETRY_DELAY", "1");
    CPLSetConfigOption("GDAL_HTTP_TIMEOUT",     "30");
    CPLSetConfigOption("GDAL_HTTP_CONNECTTIMEOUT", "10");
    CPLSetConfigOption("GDAL_HTTP_MAX_REDIRECTS", "5");
    CPLSetConfigOption("GDAL_HTTP_USERAGENT",   "FlashViewer");

    // Untrusted-data containment (FR-SEC-4 / FR-ERR-6): cap GDAL's block-cache so a
    // malformed/oversized raster cannot balloon decoder memory.
    CPLSetConfigOption("GDAL_CACHEMAX", "512");   // MB

    FV_INFO("CloudReader: GDAL VSI configured for COG access (TLS on, bounded HTTP)");
}

std::string CloudReader::vsicurlPath(const std::string& url) {
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
        return "/vsicurl/" + url;
    if (url.rfind("/vsicurl/", 0) == 0 || url.rfind("/vsis3/", 0) == 0
        || url.rfind("/vsigs/", 0) == 0)
        return url;
    return url;
}

std::shared_ptr<RasterDataset> CloudReader::open(const std::string& url) {
    // SSRF guard (FR-SEC-1/2/3): validate scheme + resolved host BEFORE any fetch.
    UrlGuard::Result guard = UrlGuard::check(url);
    if (!guard.ok) {
        ErrorReporter::instance().report(4, QStringLiteral("Security"), guard.reason);
        return nullptr;
    }
    std::string vsi = vsicurlPath(url);
    FV_INFO("CloudReader: opening '{}'", vsi);
    return RasterDataset::open(vsi);
}
