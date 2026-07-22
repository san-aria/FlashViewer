#pragma once
#include <memory>
#include <string>
#include <vector>
#include <QString>

class RasterDataset;

class DatasetFactory {
public:
    static std::shared_ptr<RasterDataset> open(const std::string& path,
                                                bool* needsBinaryDialog = nullptr);
    static std::shared_ptr<RasterDataset> openSubdataset(const std::string& subdataset_path);
    static std::vector<std::pair<std::string,std::string>>
        listSubdatasets(const std::string& path);
    static const char* openFilter();
    static bool isBinaryRaw(const std::string& path);

    // Returns true for file types that may contain multiple variables (NetCDF, HDF5).
    static bool isMultiVariableFormat(const std::string& path);

    // Extract a human-readable variable name from a GDAL subdataset path.
    // For groups, preserves the full path: "NETCDF:\"f.nc\":/grp/var" → "/grp/var"
    static QString extractVarName(const std::string& gdal_path);
};
