#pragma once
#include <string>
#include <memory>

class RasterDataset;

class CloudReader {
public:
    static void configureGdal();
    static std::shared_ptr<RasterDataset> open(const std::string& url);
    static std::string vsicurlPath(const std::string& url);
};
