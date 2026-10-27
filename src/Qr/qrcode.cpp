#include "qrcode.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace qr {
namespace {

// ---------------------------------------------------------------- constants

const int kMinVersion = 1;
const int kMaxVersion = 40;

const int kPenaltyN1 = 3;
const int kPenaltyN2 = 3;
const int kPenaltyN3 = 40;
const int kPenaltyN4 = 10;

// Index 0 is padding so that the array can be indexed by version number directly.
const int kEccCodewordsPerBlock[4][41] = {
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
          28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
          26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
          28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
          30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};

const int kNumEccBlocks[4][41] = {
    {-1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,
           8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1,  1,  1,  1,  2,  2,  4,  4,  4,  5,  5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16,
          17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1,  1,  1,  2,  2,  4,  4,  6,  6,  8,  8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20,
          23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1,  1,  1,  2,  4,  4,  4,  5,  6,  8,  8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
          25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

const wchar_t* kAlphanumeric = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// ---------------------------------------------------------------- basics

struct TooLongError {};

int formatBits(Ecc ecc) {
    switch (ecc) {
        case Ecc::Low: return 1;
        case Ecc::Medium: return 0;
        case Ecc::Quartile: return 3;
        default: return 2;
    }
}

int numRawDataModules(int version) {
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        int numAlign = version / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (version >= 7) result -= 36;
    }
    return result;
}

int numDataCodewords(int version, Ecc ecc) {
    return numRawDataModules(version) / 8 - kEccCodewordsPerBlock[(int)ecc][version] *
                                                   kNumEccBlocks[(int)ecc][version];
}

int gfMultiply(int x, int y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return z;
}

std::vector<uint8_t> reedSolomonDivisor(int degree) {
    std::vector<uint8_t> result((size_t)degree, 0);
    result[(size_t)degree - 1] = 1;
    int root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[(size_t)j] = (uint8_t)gfMultiply(result[(size_t)j], root);
            if (j + 1 < degree) result[(size_t)j] ^= result[(size_t)j + 1];
        }
        root = gfMultiply(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> reedSolomonRemainder(const uint8_t* data, int length,
                                          const std::vector<uint8_t>& divisor) {
    std::vector<uint8_t> result(divisor.size(), 0);
    for (int d = 0; d < length; ++d) {
        int factor = data[d] ^ result[0];
        std::memmove(result.data(), result.data() + 1, result.size() - 1);
        result[result.size() - 1] = 0;
        for (size_t i = 0; i < divisor.size(); ++i)
            result[i] ^= (uint8_t)gfMultiply(divisor[i], factor);
    }
    return result;
}

// ---------------------------------------------------------------- segments

struct BitBuffer {
    std::vector<uint8_t> bits;

    void append(int value, int count) {
        for (int i = count - 1; i >= 0; --i) bits.push_back((uint8_t)((value >> i) & 1));
    }
};

enum class Mode { Numeric, Alphanumeric, Byte };

int modeBits(Mode mode) {
    switch (mode) {
        case Mode::Numeric: return 0x1;
        case Mode::Alphanumeric: return 0x2;
        default: return 0x4;
    }
}

int numCharCountBits(Mode mode, int version) {
    static const int numeric[3] = {10, 12, 14};
    static const int alnum[3] = {9, 11, 13};
    static const int bytes[3] = {8, 16, 16};
    int index = (version + 7) / 17;
    switch (mode) {
        case Mode::Numeric: return numeric[index];
        case Mode::Alphanumeric: return alnum[index];
        default: return bytes[index];
    }
}

struct Segment {
    Mode mode;
    int numChars;
    std::vector<uint8_t> bits;
};

int alnumValue(wchar_t c) {
    for (int i = 0; i < 45; ++i)
        if (kAlphanumeric[i] == c) return i;
    return -1;
}

bool isNumericText(const std::wstring& text) {
    if (text.empty()) return false;
    for (wchar_t c : text)
        if (c < L'0' || c > L'9') return false;
    return true;
}

bool isAlnumText(const std::wstring& text) {
    if (text.empty()) return false;
    for (wchar_t c : text)
        if (alnumValue(c) < 0) return false;
    return true;
}

std::vector<uint8_t> toUtf8(const std::wstring& text) {
    std::vector<uint8_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t cp = (uint32_t)(unsigned short)text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            uint32_t low = (uint32_t)(unsigned short)text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            out.push_back((uint8_t)cp);
        } else if (cp < 0x800) {
            out.push_back((uint8_t)(0xC0 | (cp >> 6)));
            out.push_back((uint8_t)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((uint8_t)(0xE0 | (cp >> 12)));
            out.push_back((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((uint8_t)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((uint8_t)(0xF0 | (cp >> 18)));
            out.push_back((uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((uint8_t)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::vector<Segment> makeSegments(const std::wstring& text) {
    std::vector<Segment> segments;
    if (text.empty()) return segments;

    if (isNumericText(text)) {
        Segment seg{Mode::Numeric, (int)text.size(), {}};
        BitBuffer bb;
        for (size_t i = 0; i < text.size();) {
            int n = (int)std::min<size_t>(3, text.size() - i);
            int value = 0;
            for (int k = 0; k < n; ++k) value = value * 10 + (text[i + (size_t)k] - L'0');
            bb.append(value, n * 3 + 1);
            i += (size_t)n;
        }
        seg.bits = std::move(bb.bits);
        segments.push_back(std::move(seg));
    } else if (isAlnumText(text)) {
        Segment seg{Mode::Alphanumeric, (int)text.size(), {}};
        BitBuffer bb;
        size_t i = 0;
        for (; i + 1 < text.size(); i += 2) {
            int value = alnumValue(text[i]) * 45 + alnumValue(text[i + 1]);
            bb.append(value, 11);
        }
        if (i < text.size()) bb.append(alnumValue(text[i]), 6);
        seg.bits = std::move(bb.bits);
        segments.push_back(std::move(seg));
    } else {
        std::vector<uint8_t> bytes = toUtf8(text);
        Segment seg{Mode::Byte, (int)bytes.size(), {}};
        BitBuffer bb;
        for (uint8_t b : bytes) bb.append(b, 8);
        seg.bits = std::move(bb.bits);
        segments.push_back(std::move(seg));
    }
    return segments;
}

// -1 means "length field overflow".
int totalBits(const std::vector<Segment>& segments, int version) {
    int result = 0;
    for (const Segment& seg : segments) {
        int ccBits = numCharCountBits(seg.mode, version);
        if (seg.numChars >= (1 << ccBits)) return -1;
        result += 4 + ccBits + (int)seg.bits.size();
    }
    return result;
}

// ---------------------------------------------------------------- builder

class Builder {
public:
    Builder(int version, Ecc ecc) : version_(version), ecc_(ecc), size_(version * 4 + 17) {
        modules_.assign((size_t)size_ * size_, 0);
        isFunction_.assign((size_t)size_ * size_, 0);
    }

    // Renders the whole symbol.
    void build(const std::vector<uint8_t>& dataCodewords, int mask);

    int mask() const { return mask_; }
    const std::vector<uint8_t>& modules() const { return modules_; }
    int size() const { return size_; }

private:
    void drawFunctionPatterns();
    void drawFormatBits(int mask);
    void drawVersion();
    void drawFinderPattern(int x, int y);
    void drawAlignmentPattern(int x, int y);
    void setFunctionModule(int x, int y, bool dark);
    std::vector<uint8_t> addEccAndInterleave(const std::vector<uint8_t>& data);
    void drawCodewords(const std::vector<uint8_t>& data);
    void applyMask(int mask);
    int penaltyScore() const;
    std::vector<int> alignmentPatternPositions() const;
    void addRunHistory(int* history, int runLength) const;
    static int countFinderPatterns(const int* history);

    int version_;
    Ecc ecc_;
    int size_;
    int mask_ = 0;
    std::vector<uint8_t> modules_;
    std::vector<uint8_t> isFunction_;
};

void Builder::build(const std::vector<uint8_t>& dataCodewords, int mask) {
    drawFunctionPatterns();
    std::vector<uint8_t> all = addEccAndInterleave(dataCodewords);
    drawCodewords(all);

    if (mask < 0) {
        int minPenalty = 0x7FFFFFFF;
        for (int i = 0; i < 8; ++i) {
            applyMask(i);
            drawFormatBits(i);
            int penalty = penaltyScore();
            if (penalty < minPenalty) {
                mask = i;
                minPenalty = penalty;
            }
            applyMask(i);
        }
    }
    mask_ = mask;
    applyMask(mask_);
    drawFormatBits(mask_);
}

void Builder::drawFunctionPatterns() {
    for (int i = 0; i < size_; ++i) {
        setFunctionModule(6, i, i % 2 == 0);
        setFunctionModule(i, 6, i % 2 == 0);
    }
    drawFinderPattern(3, 3);
    drawFinderPattern(size_ - 4, 3);
    drawFinderPattern(3, size_ - 4);

    std::vector<int> pos = alignmentPatternPositions();
    int numAlign = (int)pos.size();
    for (int i = 0; i < numAlign; ++i) {
        for (int j = 0; j < numAlign; ++j) {
            bool corner = (i == 0 && j == 0) || (i == 0 && j == numAlign - 1) ||
                          (i == numAlign - 1 && j == 0);
            if (!corner) drawAlignmentPattern(pos[(size_t)i], pos[(size_t)j]);
        }
    }
    drawFormatBits(0);
    drawVersion();
}

void Builder::drawFormatBits(int mask) {
    int data = (formatBits(ecc_) << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;

    for (int i = 0; i < 6; ++i) setFunctionModule(8, i, ((bits >> i) & 1) != 0);
    setFunctionModule(8, 7, ((bits >> 6) & 1) != 0);
    setFunctionModule(8, 8, ((bits >> 7) & 1) != 0);
    setFunctionModule(7, 8, ((bits >> 8) & 1) != 0);
    for (int i = 9; i < 15; ++i) setFunctionModule(14 - i, 8, ((bits >> i) & 1) != 0);

    for (int i = 0; i < 8; ++i) setFunctionModule(size_ - 1 - i, 8, ((bits >> i) & 1) != 0);
    for (int i = 8; i < 15; ++i) setFunctionModule(8, size_ - 15 + i, ((bits >> i) & 1) != 0);
    setFunctionModule(8, size_ - 8, true);
}

void Builder::drawVersion() {
    if (version_ < 7) return;
    int rem = version_;
    for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    int bits = (version_ << 12) | rem;

    for (int i = 0; i < 18; ++i) {
        bool bit = ((bits >> i) & 1) != 0;
        int a = size_ - 11 + i % 3;
        int b = i / 3;
        setFunctionModule(a, b, bit);
        setFunctionModule(b, a, bit);
    }
}

void Builder::drawFinderPattern(int x, int y) {
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= size_ || yy >= size_) continue;
            int dist = std::max(std::abs(dx), std::abs(dy));
            setFunctionModule(xx, yy, dist != 2 && dist != 4);
        }
    }
}

void Builder::drawAlignmentPattern(int x, int y) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

void Builder::setFunctionModule(int x, int y, bool dark) {
    modules_[(size_t)y * size_ + x] = dark ? 1 : 0;
    isFunction_[(size_t)y * size_ + x] = 1;
}

std::vector<uint8_t> Builder::addEccAndInterleave(const std::vector<uint8_t>& data) {
    int numBlocks = kNumEccBlocks[(int)ecc_][version_];
    int blockEccLen = kEccCodewordsPerBlock[(int)ecc_][version_];
    int rawCodewords = numRawDataModules(version_) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    std::vector<std::vector<uint8_t>> blocks((size_t)numBlocks);
    std::vector<uint8_t> rsDiv = reedSolomonDivisor(blockEccLen);
    int k = 0;
    for (int i = 0; i < numBlocks; ++i) {
        int datLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        // One extra byte so every block lines up during interleaving; skipped below for short blocks.
        std::vector<uint8_t> blk((size_t)shortBlockLen + 1, 0);
        std::memcpy(blk.data(), data.data() + k, (size_t)datLen);
        k += datLen;
        std::vector<uint8_t> ecc = reedSolomonRemainder(blk.data(), datLen, rsDiv);
        std::memcpy(blk.data() + blk.size() - (size_t)blockEccLen, ecc.data(), (size_t)blockEccLen);
        blocks[(size_t)i] = std::move(blk);
    }

    std::vector<uint8_t> result((size_t)rawCodewords, 0);
    int pos = 0;
    for (int i = 0; i < shortBlockLen + 1; ++i) {
        for (int j = 0; j < numBlocks; ++j) {
            if (i != shortBlockLen - blockEccLen || j >= numShortBlocks)
                result[(size_t)pos++] = blocks[(size_t)j][(size_t)i];
        }
    }
    return result;
}

void Builder::drawCodewords(const std::vector<uint8_t>& data) {
    int i = 0;
    int totalBits = (int)data.size() * 8;
    for (int right = size_ - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size_; ++vert) {
            for (int j = 0; j < 2; ++j) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size_ - 1 - vert : vert;
                if (!isFunction_[(size_t)y * size_ + x] && i < totalBits) {
                    modules_[(size_t)y * size_ + x] = (data[(size_t)(i >> 3)] >> (7 - (i & 7))) & 1;
                    ++i;
                }
            }
        }
    }
}

void Builder::applyMask(int mask) {
    for (int y = 0; y < size_; ++y) {
        for (int x = 0; x < size_; ++x) {
            bool invert = false;
            switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
            }
            size_t index = (size_t)y * size_ + x;
            if (invert && !isFunction_[index]) modules_[index] ^= 1;
        }
    }
}

int Builder::countFinderPatterns(const int* h) {
    int n = h[1];
    bool core = n > 0 && h[2] == n && h[4] == n && h[5] == n && h[3] == n * 3;
    int count = 0;
    if (core && h[0] >= n * 4 && h[6] >= n) ++count;
    if (core && h[6] >= n * 4 && h[0] >= n) ++count;
    return count;
}

void Builder::addRunHistory(int* history, int runLength) const {
    if (history[0] == 0) runLength += size_;
    std::memmove(history + 1, history, sizeof(int) * 6);
    history[0] = runLength;
}

int Builder::penaltyScore() const {
    int result = 0;

    for (int y = 0; y < size_; ++y) {
        bool runColor = false;
        int run = 0;
        int history[7] = {0};
        for (int x = 0; x < size_; ++x) {
            bool color = modules_[(size_t)y * size_ + x] != 0;
            if (color == runColor) {
                ++run;
                if (run == 5) result += kPenaltyN1;
                else if (run > 5) ++result;
            } else {
                addRunHistory(history, run);
                if (!runColor) result += countFinderPatterns(history) * kPenaltyN3;
                runColor = color;
                run = 1;
            }
        }
        if (runColor) {
            addRunHistory(history, run);
            run = 0;
        }
        run += size_;
        addRunHistory(history, run);
        result += countFinderPatterns(history) * kPenaltyN3;
    }

    for (int x = 0; x < size_; ++x) {
        bool runColor = false;
        int run = 0;
        int history[7] = {0};
        for (int y = 0; y < size_; ++y) {
            bool color = modules_[(size_t)y * size_ + x] != 0;
            if (color == runColor) {
                ++run;
                if (run == 5) result += kPenaltyN1;
                else if (run > 5) ++result;
            } else {
                addRunHistory(history, run);
                if (!runColor) result += countFinderPatterns(history) * kPenaltyN3;
                runColor = color;
                run = 1;
            }
        }
        if (runColor) {
            addRunHistory(history, run);
            run = 0;
        }
        run += size_;
        addRunHistory(history, run);
        result += countFinderPatterns(history) * kPenaltyN3;
    }

    for (int y = 0; y < size_ - 1; ++y) {
        for (int x = 0; x < size_ - 1; ++x) {
            uint8_t c = modules_[(size_t)y * size_ + x];
            if (c == modules_[(size_t)y * size_ + x + 1] && c == modules_[(size_t)(y + 1) * size_ + x] &&
                c == modules_[(size_t)(y + 1) * size_ + x + 1])
                result += kPenaltyN2;
        }
    }

    int dark = 0;
    for (uint8_t m : modules_) dark += m != 0 ? 1 : 0;
    int total = size_ * size_;
    int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
    result += k * kPenaltyN4;
    return result;
}

std::vector<int> Builder::alignmentPatternPositions() const {
    std::vector<int> result;
    if (version_ == 1) return result;
    int numAlign = version_ / 7 + 2;
    int step = (version_ * 8 + numAlign * 3 + 5) / (numAlign * 4 - 4) * 2;
    result.assign((size_t)numAlign, 0);
    result[0] = 6;
    int pos = size_ - 7;
    for (int i = numAlign - 1; i >= 1; --i, pos -= step) result[(size_t)i] = pos;
    return result;
}

}  // namespace

bool encodeText(const std::wstring& text, const EncodeOptions& options, Symbol& out, std::wstring& error) {
    if (options.minVersion < kMinVersion || options.maxVersion > kMaxVersion ||
        options.minVersion > options.maxVersion || options.mask < -1 || options.mask > 7) {
        error = L"参数无效";
        return false;
    }

    std::vector<Segment> segments = makeSegments(text);
    Ecc ecc = options.ecc;

    int version = options.minVersion;
    int dataUsedBits = 0;
    bool found = false;
    for (; version <= options.maxVersion; ++version) {
        int capacityBits = numDataCodewords(version, ecc) * 8;
        int used = totalBits(segments, version);
        if (used >= 0 && used <= capacityBits) {
            dataUsedBits = used;
            found = true;
            break;
        }
        if (version == options.maxVersion) {
            error = used < 0 ? L"内容过长,超出容量" : L"内容过长,超出版本上限";
            return false;
        }
    }
    if (!found) {
        error = L"内容过长,超出版本上限";
        return false;
    }

    if (options.boostEcl) {
        const Ecc higher[] = {Ecc::Medium, Ecc::Quartile, Ecc::High};
        for (Ecc candidate : higher) {
            if (dataUsedBits <= numDataCodewords(version, candidate) * 8) ecc = candidate;
        }
    }

    BitBuffer bb;
    for (const Segment& seg : segments) {
        bb.append(modeBits(seg.mode), 4);
        bb.append(seg.numChars, numCharCountBits(seg.mode, version));
        bb.bits.insert(bb.bits.end(), seg.bits.begin(), seg.bits.end());
    }
    if ((int)bb.bits.size() != dataUsedBits) {
        error = L"内部编码错误";
        return false;
    }

    int dataCapacityBits = numDataCodewords(version, ecc) * 8;
    int terminator = std::min(4, dataCapacityBits - (int)bb.bits.size());
    bb.append(0, terminator);
    int remainder = (8 - (int)bb.bits.size() % 8) % 8;
    bb.append(0, remainder);
    for (int pad = 0xEC; (int)bb.bits.size() < dataCapacityBits; pad ^= 0xEC ^ 0x11)
        bb.append(pad, 8);

    std::vector<uint8_t> dataCodewords(bb.bits.size() / 8, 0);
    for (size_t i = 0; i < bb.bits.size(); ++i)
        if (bb.bits[i]) dataCodewords[i >> 3] |= (uint8_t)(1 << (7 - (i & 7)));

    Builder builder(version, ecc);
    builder.build(dataCodewords, options.mask);

    out.version = version;
    out.size = builder.size();
    out.mask = builder.mask();
    out.ecc = ecc;
    out.modules = builder.modules();
    return true;
}

}  // namespace qr
