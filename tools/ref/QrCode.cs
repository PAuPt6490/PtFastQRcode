// QR Code encoder (Model 2, versions 1-40, ECC levels L/M/Q/H).
//
// This is a C# port of the algorithms and tables from Project Nayuki's QR Code
// generator library (https://www.nayuki.io/page/qr-code-generator-library),
// which is distributed under the MIT License. The port keeps the same public
// behaviour: automatic minimal version selection, optional ECC boost, and
// automatic mask selection using the standard penalty rules.

using System;
using System.Collections.Generic;
using System.Text;

namespace PtFastQRcode.Qr
{
    public enum EccLevel
    {
        Low = 0,
        Medium = 1,
        Quartile = 2,
        High = 3
    }

    public sealed class DataTooLongException : Exception
    {
        public DataTooLongException(string message) : base(message) { }
    }

    /// <summary>A QR Code symbol: an immutable square grid of dark/light modules.</summary>
    public sealed class QrCode
    {
        public const int MinVersion = 1;
        public const int MaxVersion = 40;

        public int Version { get; private set; }
        public int Size { get; private set; }
        public EccLevel Ecc { get; private set; }
        public int Mask { get; private set; }

        private readonly bool[][] _modules;
        private bool[][] _isFunction;

        /// <summary>True when the module at (x, y) is dark. Out-of-range is light.</summary>
        public bool GetModule(int x, int y)
        {
            return x >= 0 && x < Size && y >= 0 && y < Size && _modules[y][x];
        }

        // ---- High level API ----

        /// <summary>
        /// Encodes a string, choosing the densest segment mode that fits and the
        /// smallest version that holds it. Raises DataTooLongException when the
        /// payload does not fit version 40.
        /// </summary>
        public static QrCode EncodeText(string text, EccLevel ecl)
        {
            return EncodeSegments(QrSegment.MakeSegments(text), ecl, MinVersion, MaxVersion, -1, true);
        }

        /// <summary>Same as EncodeText but with explicit encoding parameters (used by tests).</summary>
        public static QrCode EncodeText(string text, EccLevel ecl, int minVersion, int maxVersion, int mask, bool boostEcl)
        {
            return EncodeSegments(QrSegment.MakeSegments(text), ecl, minVersion, maxVersion, mask, boostEcl);
        }

        /// <summary>Encodes raw bytes in byte mode.</summary>
        public static QrCode EncodeBinary(byte[] data, EccLevel ecl)
        {
            return EncodeSegments(new List<QrSegment> { QrSegment.MakeBytes(data) }, ecl, MinVersion, MaxVersion, -1, true);
        }

        public static QrCode EncodeSegments(IList<QrSegment> segs, EccLevel ecl, int minVersion, int maxVersion, int mask, bool boostEcl)
        {
            if (minVersion < MinVersion || maxVersion > MaxVersion || minVersion > maxVersion || mask < -1 || mask > 7)
                throw new ArgumentOutOfRangeException("parameter", "Invalid QR encoding parameters");

            // Find the smallest version that fits the payload.
            int version;
            int dataUsedBits = 0;
            for (version = minVersion; ; version++)
            {
                int capacityBits = GetNumDataCodewords(version, ecl) * 8;
                int? used = QrSegment.GetTotalBits(segs, version);
                if (used.HasValue && used.Value <= capacityBits)
                {
                    dataUsedBits = used.Value;
                    break;
                }
                if (version >= maxVersion)
                {
                    if (used.HasValue)
                        throw new DataTooLongException("内容过长:需要 " + used.Value + " 位,当前版本最多 " + capacityBits + " 位");
                    throw new DataTooLongException("内容过长,超出二维码容量");
                }
            }

            // Raise the error correction level while the payload still fits.
            if (boostEcl)
            {
                EccLevel[] higher = { EccLevel.Medium, EccLevel.Quartile, EccLevel.High };
                foreach (EccLevel candidate in higher)
                {
                    if (dataUsedBits <= GetNumDataCodewords(version, candidate) * 8)
                        ecl = candidate;
                }
            }

            // Concatenate segments into the data bit string.
            BitBuffer bb = new BitBuffer();
            foreach (QrSegment seg in segs)
            {
                bb.AppendBits(seg.Mode.ModeBits, 4);
                bb.AppendBits(seg.NumChars, seg.Mode.NumCharCountBits(version));
                bb.AppendBits(seg.BitData);
            }
            if (bb.Count != dataUsedBits)
                throw new InvalidOperationException("Bit count mismatch");

            // Terminator, byte padding, then alternating pad bytes.
            int dataCapacityBits = GetNumDataCodewords(version, ecl) * 8;
            bb.AppendBits(0, Math.Min(4, dataCapacityBits - bb.Count));
            bb.AppendBits(0, (8 - bb.Count % 8) % 8);
            for (int pad = 0xEC; bb.Count < dataCapacityBits; pad ^= 0xEC ^ 0x11)
                bb.AppendBits(pad, 8);

            // Pack into big-endian bytes.
            int n = bb.Count / 8;
            byte[] dataCodewords = new byte[n];
            for (int i = 0; i < bb.Count; i++)
            {
                if (bb[i])
                    dataCodewords[i >> 3] |= (byte)(1 << (7 - (i & 7)));
            }

            return new QrCode(version, ecl, dataCodewords, mask);
        }

        // ---- Constructor (low level) ----

        private QrCode(int version, EccLevel ecl, byte[] dataCodewords, int mask)
        {
            if (version < MinVersion || version > MaxVersion)
                throw new ArgumentOutOfRangeException("version");
            if (mask < -1 || mask > 7)
                throw new ArgumentOutOfRangeException("mask");

            Version = version;
            Size = version * 4 + 17;
            Ecc = ecl;

            _modules = NewGrid(Size);
            _isFunction = NewGrid(Size);

            DrawFunctionPatterns();
            byte[] allCodewords = AddEccAndInterleave(dataCodewords);
            DrawCodewords(allCodewords);

            if (mask == -1)
            {
                int minPenalty = int.MaxValue;
                for (int i = 0; i < 8; i++)
                {
                    ApplyMask(i);
                    DrawFormatBits(i);
                    int penalty = GetPenaltyScore();
                    if (penalty < minPenalty)
                    {
                        mask = i;
                        minPenalty = penalty;
                    }
                    ApplyMask(i);
                }
            }
            Mask = mask;
            ApplyMask(mask);
            DrawFormatBits(mask);

            _isFunction = null;
        }

        private static bool[][] NewGrid(int size)
        {
            bool[][] grid = new bool[size][];
            for (int i = 0; i < size; i++)
                grid[i] = new bool[size];
            return grid;
        }

        // ---- Function patterns ----

        private void DrawFunctionPatterns()
        {
            for (int i = 0; i < Size; i++)
            {
                SetFunctionModule(6, i, i % 2 == 0);
                SetFunctionModule(i, 6, i % 2 == 0);
            }

            DrawFinderPattern(3, 3);
            DrawFinderPattern(Size - 4, 3);
            DrawFinderPattern(3, Size - 4);

            int[] alignPatPos = GetAlignmentPatternPositions();
            int numAlign = alignPatPos.Length;
            for (int i = 0; i < numAlign; i++)
            {
                for (int j = 0; j < numAlign; j++)
                {
                    bool onFinderCorner = (i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0);
                    if (!onFinderCorner)
                        DrawAlignmentPattern(alignPatPos[i], alignPatPos[j]);
                }
            }

            DrawFormatBits(0);
            DrawVersion();
        }

        private void DrawFormatBits(int mask)
        {
            int data = ((int)Ecc.FormatBits() << 3) | mask;
            int rem = data;
            for (int i = 0; i < 10; i++)
                rem = (rem << 1) ^ ((rem >> 9) * 0x537);
            int bits = ((data << 10) | rem) ^ 0x5412;

            for (int i = 0; i < 6; i++)
                SetFunctionModule(8, i, GetBit(bits, i));
            SetFunctionModule(8, 7, GetBit(bits, 6));
            SetFunctionModule(8, 8, GetBit(bits, 7));
            SetFunctionModule(7, 8, GetBit(bits, 8));
            for (int i = 9; i < 15; i++)
                SetFunctionModule(14 - i, 8, GetBit(bits, i));

            for (int i = 0; i < 8; i++)
                SetFunctionModule(Size - 1 - i, 8, GetBit(bits, i));
            for (int i = 8; i < 15; i++)
                SetFunctionModule(8, Size - 15 + i, GetBit(bits, i));
            SetFunctionModule(8, Size - 8, true);
        }

        private void DrawVersion()
        {
            if (Version < 7)
                return;

            int rem = Version;
            for (int i = 0; i < 12; i++)
                rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
            int bits = (Version << 12) | rem;

            for (int i = 0; i < 18; i++)
            {
                bool bit = GetBit(bits, i);
                int a = Size - 11 + i % 3;
                int b = i / 3;
                SetFunctionModule(a, b, bit);
                SetFunctionModule(b, a, bit);
            }
        }

        private void DrawFinderPattern(int x, int y)
        {
            for (int dy = -4; dy <= 4; dy++)
            {
                for (int dx = -4; dx <= 4; dx++)
                {
                    int xx = x + dx;
                    int yy = y + dy;
                    if (xx >= 0 && xx < Size && yy >= 0 && yy < Size)
                    {
                        int dist = Math.Max(Math.Abs(dx), Math.Abs(dy));
                        SetFunctionModule(xx, yy, dist != 2 && dist != 4);
                    }
                }
            }
        }

        private void DrawAlignmentPattern(int x, int y)
        {
            for (int dy = -2; dy <= 2; dy++)
            {
                for (int dx = -2; dx <= 2; dx++)
                    SetFunctionModule(x + dx, y + dy, Math.Max(Math.Abs(dx), Math.Abs(dy)) != 1);
            }
        }

        private void SetFunctionModule(int x, int y, bool isDark)
        {
            _modules[y][x] = isDark;
            _isFunction[y][x] = true;
        }

        // ---- Codewords and masking ----

        private byte[] AddEccAndInterleave(byte[] data)
        {
            if (data.Length != GetNumDataCodewords(Version, Ecc))
                throw new ArgumentException("Invalid data length", "data");

            int numBlocks = NumErrorCorrectionBlocks[(int)Ecc][Version];
            int blockEccLen = EccCodewordsPerBlock[(int)Ecc][Version];
            int rawCodewords = GetNumRawDataModules(Version) / 8;
            int numShortBlocks = numBlocks - rawCodewords % numBlocks;
            int shortBlockLen = rawCodewords / numBlocks;

            byte[][] blocks = new byte[numBlocks][];
            byte[] rsDiv = ReedSolomonComputeDivisor(blockEccLen);
            int k = 0;
            for (int i = 0; i < numBlocks; i++)
            {
                int datLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
                byte[] dat = new byte[datLen + 1];
                Array.Copy(data, k, dat, 0, datLen);
                k += datLen;
                byte[] ecc = ReedSolomonComputeRemainder(dat, datLen, rsDiv);

                // Every block is stored one byte longer than its data: short blocks keep a
                // placeholder pad byte at index (shortBlockLen - blockEccLen) so that all
                // blocks line up during interleaving. That slot is skipped below.
                byte[] blk = new byte[shortBlockLen + 1];
                Array.Copy(dat, 0, blk, 0, datLen + 1);
                Array.Copy(ecc, 0, blk, blk.Length - blockEccLen, blockEccLen);
                blocks[i] = blk;
            }
            if (k != data.Length)
                throw new InvalidOperationException("Interleave input mismatch");

            byte[] result = new byte[rawCodewords];
            int pos = 0;
            for (int i = 0; i < blocks[0].Length; i++)
            {
                for (int j = 0; j < numBlocks; j++)
                {
                    // Short blocks carry a pad byte at this index; skip it.
                    if (i != shortBlockLen - blockEccLen || j >= numShortBlocks)
                        result[pos++] = blocks[j][i];
                }
            }
            if (pos != rawCodewords)
                throw new InvalidOperationException("Interleave output mismatch");
            return result;
        }

        private void DrawCodewords(byte[] data)
        {
            int i = 0;
            for (int right = Size - 1; right >= 1; right -= 2)
            {
                if (right == 6)
                    right = 5;
                for (int vert = 0; vert < Size; vert++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        int x = right - j;
                        bool upward = ((right + 1) & 2) == 0;
                        int y = upward ? Size - 1 - vert : vert;
                        if (!_isFunction[y][x] && i < data.Length * 8)
                        {
                            _modules[y][x] = GetBit(data[i >> 3], 7 - (i & 7));
                            i++;
                        }
                    }
                }
            }
            if (i != data.Length * 8)
                throw new InvalidOperationException("Codeword placement mismatch");
        }

        private void ApplyMask(int mask)
        {
            if (mask < 0 || mask > 7)
                throw new ArgumentOutOfRangeException("mask");

            for (int y = 0; y < Size; y++)
            {
                for (int x = 0; x < Size; x++)
                {
                    bool invert;
                    switch (mask)
                    {
                        case 0: invert = (x + y) % 2 == 0; break;
                        case 1: invert = y % 2 == 0; break;
                        case 2: invert = x % 3 == 0; break;
                        case 3: invert = (x + y) % 3 == 0; break;
                        case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                        case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                        case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                        default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                    }
                    if (invert && !_isFunction[y][x])
                        _modules[y][x] = !_modules[y][x];
                }
            }
        }

        private int GetPenaltyScore()
        {
            const int PenaltyN1 = 3;
            const int PenaltyN2 = 3;
            const int PenaltyN3 = 40;
            const int PenaltyN4 = 10;

            int result = 0;

            for (int y = 0; y < Size; y++)
            {
                bool runColor = false;
                int runX = 0;
                RunHistory hist = new RunHistory();
                for (int x = 0; x < Size; x++)
                {
                    if (_modules[y][x] == runColor)
                    {
                        runX++;
                        if (runX == 5)
                            result += PenaltyN1;
                        else if (runX > 5)
                            result++;
                    }
                    else
                    {
                        AddRunHistory(hist, runX);
                        if (!runColor)
                            result += CountFinderPatterns(hist) * PenaltyN3;
                        runColor = _modules[y][x];
                        runX = 1;
                    }
                }
                result += TerminateAndCount(runColor, runX, hist) * PenaltyN3;
            }

            for (int x = 0; x < Size; x++)
            {
                bool runColor = false;
                int runY = 0;
                RunHistory hist = new RunHistory();
                for (int y = 0; y < Size; y++)
                {
                    if (_modules[y][x] == runColor)
                    {
                        runY++;
                        if (runY == 5)
                            result += PenaltyN1;
                        else if (runY > 5)
                            result++;
                    }
                    else
                    {
                        AddRunHistory(hist, runY);
                        if (!runColor)
                            result += CountFinderPatterns(hist) * PenaltyN3;
                        runColor = _modules[y][x];
                        runY = 1;
                    }
                }
                result += TerminateAndCount(runColor, runY, hist) * PenaltyN3;
            }

            for (int y = 0; y < Size - 1; y++)
            {
                for (int x = 0; x < Size - 1; x++)
                {
                    bool c = _modules[y][x];
                    if (c == _modules[y][x + 1] && c == _modules[y + 1][x] && c == _modules[y + 1][x + 1])
                        result += PenaltyN2;
                }
            }

            int dark = 0;
            for (int y = 0; y < Size; y++)
            {
                for (int x = 0; x < Size; x++)
                {
                    if (_modules[y][x])
                        dark++;
                }
            }
            int total = Size * Size;
            int k = (Math.Abs(dark * 20 - total * 10) + total - 1) / total - 1;
            result += k * PenaltyN4;
            return result;
        }

        private sealed class RunHistory
        {
            public readonly int[] Data = new int[7];

            public void Push(int value)
            {
                for (int i = Data.Length - 1; i > 0; i--)
                    Data[i] = Data[i - 1];
                Data[0] = value;
            }
        }

        private void AddRunHistory(RunHistory hist, int runLength)
        {
            if (hist.Data[0] == 0)
                runLength += Size;
            hist.Push(runLength);
        }

        private int CountFinderPatterns(RunHistory hist)
        {
            int[] h = hist.Data;
            int n = h[1];
            bool core = n > 0 && h[2] == n && h[4] == n && h[5] == n && h[3] == n * 3;
            int count = 0;
            if (core && h[0] >= n * 4 && h[6] >= n)
                count++;
            if (core && h[6] >= n * 4 && h[0] >= n)
                count++;
            return count;
        }

        private int TerminateAndCount(bool currentRunColor, int currentRunLength, RunHistory hist)
        {
            if (currentRunColor)
            {
                AddRunHistory(hist, currentRunLength);
                currentRunLength = 0;
            }
            currentRunLength += Size;
            AddRunHistory(hist, currentRunLength);
            return CountFinderPatterns(hist);
        }

        // ---- Tables and arithmetic ----

        private int[] GetAlignmentPatternPositions()
        {
            if (Version == 1)
                return new int[0];

            int numAlign = Version / 7 + 2;
            int step = (Version * 8 + numAlign * 3 + 5) / (numAlign * 4 - 4) * 2;
            int[] result = new int[numAlign];
            result[0] = 6;
            for (int i = numAlign - 1, pos = Size - 7; i >= 1; i--, pos -= step)
                result[i] = pos;
            return result;
        }

        internal static int GetNumRawDataModules(int version)
        {
            if (version < MinVersion || version > MaxVersion)
                throw new ArgumentOutOfRangeException("version");

            int result = (16 * version + 128) * version + 64;
            if (version >= 2)
            {
                int numAlign = version / 7 + 2;
                result -= (25 * numAlign - 10) * numAlign - 55;
                if (version >= 7)
                    result -= 36;
            }
            return result;
        }

        internal static int GetNumDataCodewords(int version, EccLevel ecl)
        {
            return GetNumRawDataModules(version) / 8
                - EccCodewordsPerBlock[(int)ecl][version]
                * NumErrorCorrectionBlocks[(int)ecl][version];
        }

        private static byte[] ReedSolomonComputeDivisor(int degree)
        {
            if (degree < 1 || degree > 255)
                throw new ArgumentOutOfRangeException("degree");

            byte[] result = new byte[degree];
            result[degree - 1] = 1;

            int root = 1;
            for (int i = 0; i < degree; i++)
            {
                for (int j = 0; j < degree; j++)
                {
                    result[j] = (byte)ReedSolomonMultiply(result[j], root);
                    if (j + 1 < degree)
                        result[j] ^= result[j + 1];
                }
                root = ReedSolomonMultiply(root, 0x02);
            }
            return result;
        }

        private static byte[] ReedSolomonComputeRemainder(byte[] data, int dataLen, byte[] divisor)
        {
            byte[] result = new byte[divisor.Length];
            for (int d = 0; d < dataLen; d++)
            {
                int factor = data[d] ^ result[0];
                Array.Copy(result, 1, result, 0, result.Length - 1);
                result[result.Length - 1] = 0;
                for (int i = 0; i < divisor.Length; i++)
                    result[i] ^= (byte)ReedSolomonMultiply(divisor[i], factor);
            }
            return result;
        }

        private static int ReedSolomonMultiply(int x, int y)
        {
            int z = 0;
            for (int i = 7; i >= 0; i--)
            {
                z = (z << 1) ^ ((z >> 7) * 0x11D);
                z ^= ((y >> i) & 1) * x;
            }
            return z;
        }

        private static bool GetBit(int x, int i)
        {
            return ((x >> i) & 1) != 0;
        }

        private static readonly int[][] EccCodewordsPerBlock = new int[][]
        {
            // 0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38  39  40
            new int[] { -1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 }, // Low
            new int[] { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28 }, // Medium
            new int[] { -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 }, // Quartile
            new int[] { -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 }  // High
        };

        private static readonly int[][] NumErrorCorrectionBlocks = new int[][]
        {
            // 0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38  39  40
            new int[] { -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25 }, // Low
            new int[] { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 }, // Medium
            new int[] { -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 }, // Quartile
            new int[] { -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 }  // High
        };
    }

    internal static class EccExtensions
    {
        /// <summary>The 2-bit format field value of this error correction level.</summary>
        public static int FormatBits(this EccLevel level)
        {
            switch (level)
            {
                case EccLevel.Low: return 1;
                case EccLevel.Medium: return 0;
                case EccLevel.Quartile: return 3;
                default: return 2;
            }
        }
    }

    /// <summary>A growable sequence of bits, used to build segment and payload bit strings.</summary>
    internal sealed class BitBuffer : List<bool>
    {
        public void AppendBits(int value, int count)
        {
            if (count < 0 || count > 31 || (count < 31 && (value >> count) != 0))
                throw new ArgumentOutOfRangeException("value");
            for (int i = count - 1; i >= 0; i--)
                Add(((value >> i) & 1) != 0);
        }

        public void AppendBits(IList<bool> bits)
        {
            foreach (bool b in bits)
                Add(b);
        }
    }

    /// <summary>A segment of payload data in one QR encoding mode.</summary>
    public sealed class QrSegment
    {
        public QrSegmentMode Mode { get; private set; }
        public int NumChars { get; private set; }
        internal List<bool> BitData { get; private set; }

        private QrSegment(QrSegmentMode mode, int numChars, List<bool> bitData)
        {
            if (numChars < 0)
                throw new ArgumentOutOfRangeException("numChars");
            Mode = mode;
            NumChars = numChars;
            BitData = bitData;
        }

        public static QrSegment MakeBytes(byte[] data)
        {
            BitBuffer bb = new BitBuffer();
            foreach (byte b in data)
                bb.AppendBits(b, 8);
            return new QrSegment(QrSegmentMode.Byte, data.Length, bb);
        }

        public static QrSegment MakeNumeric(string digits)
        {
            if (!IsNumeric(digits))
                throw new ArgumentException("String contains non-numeric characters", "digits");

            BitBuffer bb = new BitBuffer();
            int i = 0;
            while (i < digits.Length)
            {
                int n = Math.Min(digits.Length - i, 3);
                bb.AppendBits(int.Parse(digits.Substring(i, n)), n * 3 + 1);
                i += n;
            }
            return new QrSegment(QrSegmentMode.Numeric, digits.Length, bb);
        }

        public static QrSegment MakeAlphanumeric(string text)
        {
            if (!IsAlphanumeric(text))
                throw new ArgumentException("Unencodable characters for alphanumeric mode", "text");

            BitBuffer bb = new BitBuffer();
            int i;
            for (i = 0; i + 1 < text.Length; i += 2)
            {
                int temp = AlphanumericTable.IndexOf(text[i]) * 45;
                temp += AlphanumericTable.IndexOf(text[i + 1]);
                bb.AppendBits(temp, 11);
            }
            if (i < text.Length)
                bb.AppendBits(AlphanumericTable.IndexOf(text[i]), 6);
            return new QrSegment(QrSegmentMode.Alphanumeric, text.Length, bb);
        }

        /// <summary>Picks the densest mode for this text: numeric, then alphanumeric, then UTF-8 bytes.</summary>
        public static IList<QrSegment> MakeSegments(string text)
        {
            List<QrSegment> result = new List<QrSegment>();
            if (string.IsNullOrEmpty(text))
                return result;
            if (IsNumeric(text))
                result.Add(MakeNumeric(text));
            else if (IsAlphanumeric(text))
                result.Add(MakeAlphanumeric(text));
            else
                result.Add(MakeBytes(Encoding.UTF8.GetBytes(text)));
            return result;
        }

        public static bool IsNumeric(string text)
        {
            if (text.Length == 0)
                return false;
            foreach (char c in text)
            {
                if (c < '0' || c > '9')
                    return false;
            }
            return true;
        }

        public static bool IsAlphanumeric(string text)
        {
            if (text.Length == 0)
                return false;
            foreach (char c in text)
            {
                if (AlphanumericTable.IndexOf(c) < 0)
                    return false;
            }
            return true;
        }

        internal static int? GetTotalBits(IList<QrSegment> segs, int version)
        {
            int result = 0;
            foreach (QrSegment seg in segs)
            {
                int ccBits = seg.Mode.NumCharCountBits(version);
                if (seg.NumChars >= (1 << ccBits))
                    return null;
                result += 4 + ccBits + seg.BitData.Count;
            }
            return result;
        }

        private const string AlphanumericTable = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    }

    /// <summary>Describes how a segment's data bits are interpreted.</summary>
    public sealed class QrSegmentMode
    {
        public int ModeBits { get; private set; }
        private readonly int[] _charCounts;

        private QrSegmentMode(int modeBits, int[] charCounts)
        {
            ModeBits = modeBits;
            _charCounts = charCounts;
        }

        public int NumCharCountBits(int version)
        {
            return _charCounts[(version + 7) / 17];
        }

        public static readonly QrSegmentMode Numeric = new QrSegmentMode(0x1, new int[] { 10, 12, 14 });
        public static readonly QrSegmentMode Alphanumeric = new QrSegmentMode(0x2, new[] { 9, 11, 13 });
        public static readonly QrSegmentMode Byte = new QrSegmentMode(0x4, new[] { 8, 16, 16 });
        public static readonly QrSegmentMode Kanji = new QrSegmentMode(0x8, new[] { 8, 10, 12 });
        public static readonly QrSegmentMode Eci = new QrSegmentMode(0x7, new[] { 0, 0, 0 });
    }
}
