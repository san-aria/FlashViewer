#include "io/BinaryRasterParser.hpp"
#include "util/Logger.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>

namespace {

std::string gdalDtype(const std::string& dtype) {
    if (dtype == "uint8"  || dtype == "byte")   return "Byte";
    if (dtype == "int16"  || dtype == "int16")  return "Int16";
    if (dtype == "uint16")                       return "UInt16";
    if (dtype == "int32")                        return "Int32";
    if (dtype == "uint32")                       return "UInt32";
    if (dtype == "float32")                      return "Float32";
    if (dtype == "float64")                      return "Float64";
    return "Float32";
}

int bytesPerSample(const std::string& dtype) {
    if (dtype == "uint8" || dtype == "byte")   return 1;
    if (dtype == "int16" || dtype == "uint16") return 2;
    if (dtype == "int32" || dtype == "uint32" || dtype == "float32") return 4;
    if (dtype == "float64")                    return 8;
    return 4;
}

} // namespace

std::string createVrtForBinary(const BinaryRasterSpec& spec) {
    if (spec.lines <= 0 || spec.samples <= 0 || spec.bands <= 0) {
        FV_ERROR("BinaryRasterParser: invalid dimensions {}x{}x{}",
                 spec.samples, spec.lines, spec.bands);
        return {};
    }

    std::filesystem::path bin_path(spec.file_path);
    std::filesystem::path vrt_path = bin_path;
    vrt_path.replace_extension(".vrt");

    const std::string gdt = gdalDtype(spec.dtype);
    const int bps = bytesPerSample(spec.dtype);

    int pixel_offset_bytes{0}, line_offset_bytes{0}, band_offset_bytes{0};

    switch (spec.interleave) {
    case BilInterleave::BSQ:
        pixel_offset_bytes = bps;
        line_offset_bytes  = spec.samples * bps;
        band_offset_bytes  = spec.samples * spec.lines * bps;
        break;
    case BilInterleave::BIL:
        pixel_offset_bytes = bps;
        line_offset_bytes  = spec.samples * spec.bands * bps;
        band_offset_bytes  = spec.samples * bps;
        break;
    case BilInterleave::BIP:
        pixel_offset_bytes = spec.bands * bps;
        line_offset_bytes  = spec.samples * spec.bands * bps;
        band_offset_bytes  = bps;
        break;
    }

    const std::string byteOrder = spec.big_endian ? "MSB" : "LSB";
    // The VRT is written next to the .bin (same dir, same stem), so reference the
    // source RELATIVELY (basename, relativeToVRT="1"). Modern GDAL rejects a raw
    // VRTRawRasterBand whose source is an absolute path with relativeToVRT="0"
    // unless GDAL_VRT_RAWRASTERBAND_ALLOWED_SOURCE is set; a relative sibling path
    // satisfies the default SIBLING_OR_CHILD_OF_VRT_PATH policy securely.
    const std::string rel_source = bin_path.filename().string();

    std::ostringstream xml;
    xml << "<VRTDataset rasterXSize=\"" << spec.samples
        << "\" rasterYSize=\"" << spec.lines << "\">\n";

    for (int b = 1; b <= spec.bands; ++b) {
        long long image_offset = spec.header_offset
            + static_cast<long long>(b - 1) * band_offset_bytes;
        if (spec.interleave != BilInterleave::BSQ)
            image_offset = spec.header_offset + (b - 1) * bps;

        xml << "  <VRTRasterBand dataType=\"" << gdt
            << "\" band=\"" << b << "\" subClass=\"VRTRawRasterBand\">\n";
        xml << "    <SourceFilename relativeToVRT=\"1\">" << rel_source << "</SourceFilename>\n";
        xml << "    <ImageOffset>" << image_offset << "</ImageOffset>\n";
        xml << "    <PixelOffset>" << pixel_offset_bytes << "</PixelOffset>\n";
        xml << "    <LineOffset>" << line_offset_bytes << "</LineOffset>\n";
        xml << "    <ByteOrder>" << byteOrder << "</ByteOrder>\n";
        xml << "  </VRTRasterBand>\n";
    }
    xml << "</VRTDataset>\n";

    std::ofstream out(vrt_path);
    if (!out) {
        FV_ERROR("BinaryRasterParser: cannot write VRT to '{}'", vrt_path.string());
        return {};
    }
    out << xml.str();
    FV_INFO("BinaryRasterParser: created VRT '{}' for '{}'",
            vrt_path.string(), spec.file_path);
    return vrt_path.string();
}
