#pragma once
#include <string>
#include <cstdint>

enum class BilInterleave { BSQ, BIL, BIP };

struct BinaryRasterSpec {
    std::string    file_path;
    int            lines{0};
    int            samples{0};
    int            bands{1};
    BilInterleave  interleave{BilInterleave::BSQ};
    std::string    dtype{"float32"};
    long long      header_offset{0};
    bool           big_endian{false};
    std::string    description;
};

std::string createVrtForBinary(const BinaryRasterSpec& spec);
