// Cross-check harness: emits QR matrices in the same text format as the C# harness so the
// two implementations can be diffed byte for byte.
// Usage: QrTestCpp.exe <cases.txt> auto <out.txt>
//        QrTestCpp.exe <cases.txt> forced <L|M|Q|H> <out.txt>
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/qr/qrcode.h"

namespace {

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<uint8_t> base64Decode(const std::string& text) {
    std::vector<uint8_t> out;
    int buffer = 0, bits = 0;
    for (char c : text) {
        int value = base64Value(c);
        if (value < 0) continue;
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], n);
    return out;
}

const char* eccName(qr::Ecc ecc) {
    switch (ecc) {
        case qr::Ecc::Low: return "Low";
        case qr::Ecc::Medium: return "Medium";
        case qr::Ecc::Quartile: return "Quartile";
        default: return "High";
    }
}

bool parseEcc(const std::string& text, qr::Ecc& ecc) {
    if (text == "L") ecc = qr::Ecc::Low;
    else if (text == "M") ecc = qr::Ecc::Medium;
    else if (text == "Q") ecc = qr::Ecc::Quartile;
    else if (text == "H") ecc = qr::Ecc::High;
    else return false;
    return true;
}

void emit(std::string& out, int index, qr::Ecc ecc, const qr::Symbol& symbol) {
    char header[128];
    std::snprintf(header, sizeof(header), "CASE %d %s V %d M %d S %d\n", index, eccName(ecc),
                  symbol.version, symbol.mask, symbol.size);
    out += header;
    for (int y = 0; y < symbol.size; ++y) {
        for (int x = 0; x < symbol.size; ++x) out += symbol.get(x, y) ? '1' : '0';
        out += '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: QrTestCpp <cases.txt> auto|forced [L|M|Q|H] <out.txt>\n");
        return 2;
    }

    const std::string casesPath = argv[1];
    const std::string mode = argv[2];
    const bool forced = mode == "forced";
    qr::Ecc forcedEcc = qr::Ecc::Low;
    if (forced && argc >= 5 && !parseEcc(argv[3], forcedEcc)) {
        std::fprintf(stderr, "bad ecl\n");
        return 2;
    }
    const std::string outPath = argv[argc - 1];

    std::ifstream in(casesPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", casesPath.c_str());
        return 1;
    }

    std::string out;
    std::string line;
    int index = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<uint8_t> raw = line == "-" ? std::vector<uint8_t>() : base64Decode(line);
        std::wstring text = utf8ToWide(std::string(raw.begin(), raw.end()));

        if (!forced) {
            const qr::Ecc levels[] = {qr::Ecc::Low, qr::Ecc::Medium, qr::Ecc::Quartile, qr::Ecc::High};
            for (qr::Ecc ecc : levels) {
                qr::Symbol symbol;
                std::wstring error;
                qr::EncodeOptions options;
                options.ecc = ecc;
                if (qr::encodeText(text, options, symbol, error)) {
                    emit(out, index, ecc, symbol);
                } else {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "CASE %d %s ERROR too_long\n", index, eccName(ecc));
                    out += buf;
                }
            }
        } else {
            qr::Symbol probe;
            std::wstring error;
            qr::EncodeOptions options;
            options.ecc = forcedEcc;
            if (!qr::encodeText(text, options, probe, error)) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "CASE %d %s ERROR too_long\n", index, eccName(forcedEcc));
                out += buf;
                ++index;
                continue;
            }
            for (int mask = 0; mask < 8; ++mask) {
                qr::Symbol symbol;
                qr::EncodeOptions masked;
                masked.ecc = forcedEcc;
                masked.minVersion = probe.version;
                masked.maxVersion = probe.version;
                masked.mask = mask;
                masked.boostEcl = false;
                if (!qr::encodeText(text, masked, symbol, error)) {
                    std::fprintf(stderr, "unexpected failure at case %d mask %d\n", index, mask);
                    return 1;
                }
                emit(out, index, forcedEcc, symbol);
            }
        }
        ++index;
    }

    std::ofstream file(outPath, std::ios::binary);
    file.write(out.data(), (std::streamsize)out.size());
    std::printf("cases=%d\n", index);
    return 0;
}
