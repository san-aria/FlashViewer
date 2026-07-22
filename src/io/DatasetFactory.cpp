#include "io/DatasetFactory.hpp"
#include "io/RasterDataset.hpp"
#include "io/CloudReader.hpp"
#include "util/Logger.hpp"

#include <gdal_priv.h>
#include <algorithm>
#include <filesystem>

static bool isUrl(const std::string& path) {
    return path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0
        || path.rfind("/vsicurl/", 0) == 0 || path.rfind("/vsis3/", 0) == 0
        || path.rfind("/vsigs/", 0) == 0;
}

bool DatasetFactory::isBinaryRaw(const std::string& path) {
    std::string ext;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == "bin" || ext == "raw" || ext == "dat";
}

bool DatasetFactory::isMultiVariableFormat(const std::string& path) {
    std::string ext;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == "nc" || ext == "hdf" || ext == "hdf5" || ext == "h5";
}

std::shared_ptr<RasterDataset> DatasetFactory::open(
    const std::string& path, bool* needsBinaryDialog)
{
    if (needsBinaryDialog) *needsBinaryDialog = false;

    if (isUrl(path)) {
        FV_DEBUG("DatasetFactory: URL detected, using CloudReader");
        return CloudReader::open(path);
    }

    if (isBinaryRaw(path)) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            FV_DEBUG("DatasetFactory: raw binary '{}', needs import dialog", path);
            if (needsBinaryDialog) *needsBinaryDialog = true;
            return nullptr;
        }
    }

    FV_DEBUG("DatasetFactory::open('{}')", path);
    return RasterDataset::open(path);
}

std::shared_ptr<RasterDataset> DatasetFactory::openSubdataset(
    const std::string& subdataset_path)
{
    FV_DEBUG("DatasetFactory::openSubdataset('{}')", subdataset_path);
    return RasterDataset::open(subdataset_path);
}

std::vector<std::pair<std::string,std::string>>
DatasetFactory::listSubdatasets(const std::string& path) {
    std::vector<std::pair<std::string,std::string>> result;
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(
        GDALOpenEx(path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    if (!ds) return result;

    CSLConstList subdatasets = ds->GetMetadata("SUBDATASETS");
    if (subdatasets) {
        for (int i = 0; subdatasets[i] != nullptr; i += 2) {
            if (subdatasets[i] == nullptr || subdatasets[i+1] == nullptr) break;
            std::string name = subdatasets[i];
            std::string desc = subdatasets[i+1];
            auto eq = name.find('=');
            if (eq != std::string::npos) name = name.substr(eq + 1);
            eq = desc.find('=');
            if (eq != std::string::npos) desc = desc.substr(eq + 1);
            result.emplace_back(name, desc);
        }
    }
    GDALClose(ds);
    return result;
}

QString DatasetFactory::extractVarName(const std::string& gdal_path) {
    QString p = QString::fromStdString(gdal_path);
    // HDF5-style: "HDF5:\"file.h5\"://group/subgroup/var" → preserve "group/subgroup/var"
    if (p.contains("://")) {
        int idx = p.lastIndexOf("://");
        return p.mid(idx + 3);
    }
    // NETCDF-style: "NETCDF:\"file.nc\":/group/var" or "NETCDF:\"file.nc\":var"
    // Preserve the full last colon-separated segment (may include group path '/')
    return p.section(':', -1);
}

const char* DatasetFactory::openFilter() {
    return
        "All supported rasters (*.tif *.tiff *.geotiff *.nc *.hdf *.hdf5 *.h5 "
                                "*.img *.vrt *.jp2 *.bin *.raw *.dat);;"
        "GeoTIFF / COG (*.tif *.tiff *.geotiff);;"
        "NetCDF (*.nc);;"
        "HDF5 (*.hdf *.hdf5 *.h5);;"
        "ENVI Image (*.img);;"
        "Binary Raw (*.bin *.raw *.dat);;"
        "VRT (*.vrt);;"
        "JPEG2000 (*.jp2);;"
        "All files (*)";
}
