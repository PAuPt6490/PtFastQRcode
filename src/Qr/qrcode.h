// FastQRcode - QR Code encoder (Model 2, versions 1-40, ECC L/M/Q/H).
//
// C++ port of the algorithms and tables from Project Nayuki's QR Code generator
// library (https://www.nayuki.io/page/qr-code-generator-library, MIT License).
// The upstream Python implementation was used to verify this port module by module.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qr {

enum class Ecc : int { Low = 0, Medium = 1, Quartile = 2, High = 3 };

struct Symbol {
    int version = 0;
    int size = 0;
    int mask = 0;
    Ecc ecc = Ecc::Medium;
    std::vector<uint8_t> modules;  // row-major, 1 = dark module

    bool get(int x, int y) const {
        return x >= 0 && y >= 0 && x < size && y < size && modules[(size_t)y * size + x] != 0;
    }
};

struct EncodeOptions {
    Ecc ecc = Ecc::Medium;
    int minVersion = 1;
    int maxVersion = 40;
    int mask = -1;          // -1 = pick automatically
    bool boostEcl = true;   // raise ECC level when it costs no extra version
};

// Encodes Unicode text, picking the densest segment mode (numeric / alphanumeric / UTF-8 byte).
// Returns false when the payload cannot fit and fills 'error' with a human readable reason.
bool encodeText(const std::wstring& text, const EncodeOptions& options, Symbol& out, std::wstring& error);

// Convenience overload used by the app.
inline bool encodeText(const std::wstring& text, Ecc ecc, Symbol& out, std::wstring& error) {
    EncodeOptions options;
    options.ecc = ecc;
    return encodeText(text, options, out, error);
}

}  // namespace qr
