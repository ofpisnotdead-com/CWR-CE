#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Graphics/Rendering/Font/Pactext.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <ctype.h>

namespace Poseidon
{

AlphaStats ClassifyAlpha(const uint8_t* rgba, size_t pixelCount, double partialThreshold, double clearThreshold)
{
    AlphaStats s;
    if (!rgba || pixelCount == 0)
        return s;
    size_t clear = 0, opaque = 0, partial = 0;
    int aMin = 255, aMax = 0;
    unsigned long long aSum = 0;
    for (size_t i = 0; i < pixelCount; i++)
    {
        const int a = rgba[i * 4 + 3];
        aSum += static_cast<unsigned long long>(a);
        if (a < aMin)
            aMin = a;
        if (a > aMax)
            aMax = a;
        if (a == 0)
            clear++;
        else if (a == 255)
            opaque++;
        else
            partial++;
    }
    s.aMin = aMin;
    s.aMax = aMax;
    s.aMean = static_cast<int>(aSum / pixelCount);
    s.pctClear = 100.0 * static_cast<double>(clear) / static_cast<double>(pixelCount);
    s.pctOpaque = 100.0 * static_cast<double>(opaque) / static_cast<double>(pixelCount);
    s.pctPartial = 100.0 * static_cast<double>(partial) / static_cast<double>(pixelCount);
    // Partial alpha → genuine blend (must be drawn back-to-front, no depth-write).
    // Otherwise fully-transparent holes → alpha-test cutout. Otherwise opaque.
    // Both Cutout and Opaque occlude (write depth); the holes are handled by the test.
    if (s.pctPartial >= partialThreshold)
        s.kind = AlphaStats::Blend;
    else if (s.pctClear >= clearThreshold)
        s.kind = AlphaStats::Cutout;
    else
        s.kind = AlphaStats::Opaque;
    return s;
}

AlphaStats::Kind ClassifyTextureAlpha(bool hasAlpha, bool isChromaKey, bool oneBitAlphaFormat,
                                      const AlphaStats* decoded)
{
    if (!hasAlpha)
        return isChromaKey ? AlphaStats::Cutout : AlphaStats::Opaque;
    if (oneBitAlphaFormat)
        return AlphaStats::Cutout; // DXT1 etc.: punch-through holes, never a partial blend
    if (decoded)
        return decoded->kind;
    return AlphaStats::Opaque; // undecodable multi-bit alpha: keep current behavior (occlude/batch)
}

const char* AlphaKindName(AlphaStats::Kind kind)
{
    switch (kind)
    {
        case AlphaStats::Opaque:
            return "OPAQUE";
        case AlphaStats::Cutout:
            return "CUTOUT (alpha-test - fully-transparent holes)";
        case AlphaStats::Blend:
            return "BLEND (translucent - partial alpha)";
    }
    return "OPAQUE";
}

// Format name lookup from 2-byte magic
static const char* fmtName(uint16_t magic)
{
    switch (magic)
    {
        case 0x8080:
            return "AI88";
        case 0x4444:
            return "ARGB4444";
        case 0x1555:
            return "ARGB1555";
        case 0x8888:
            return "ARGB8888";
        case 0xFF01:
            return "DXT1";
        case 0xFF02:
            return "DXT2";
        case 0xFF03:
            return "DXT3";
        case 0xFF04:
            return "DXT4";
        case 0xFF05:
            return "DXT5";
        default:
            return nullptr;
    }
}

static PacFormat fmtFromMagic(uint16_t magic)
{
    switch (magic)
    {
        case 0x8080:
            return PacAI88;
        case 0x4444:
            return PacARGB4444;
        case 0x1555:
            return PacARGB1555;
        case 0x8888:
            return PacARGB8888;
        case 0xFF01:
            return PacDXT1;
        case 0xFF02:
            return PacDXT2;
        case 0xFF03:
            return PacDXT3;
        case 0xFF04:
            return PacDXT4;
        case 0xFF05:
            return PacDXT5;
        default:
            return PacFormatN;
    }
}

static uint16_t readU16(std::ifstream& f)
{
    uint16_t v = 0;
    f.read(reinterpret_cast<char*>(&v), 2);
    return v;
}

static uint32_t readU32(std::ifstream& f)
{
    uint32_t v = 0;
    f.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

static bool detectIsPaa(const std::string& path)
{
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    auto ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".paa";
}

// --- Pixel format converters ---

static void argb1555ToRGBA(const uint16_t* src, uint8_t* dst, int w, int h, int pitchBytes)
{
    for (int y = 0; y < h; y++)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(src) + y * pitchBytes);
        uint8_t* out = dst + y * w * 4;
        for (int x = 0; x < w; x++)
        {
            uint16_t p = row[x];
            uint8_t r = ((p >> 10) & 0x1F);
            uint8_t g = ((p >> 5) & 0x1F);
            uint8_t b = (p & 0x1F);
            out[x * 4 + 0] = (r << 3) | (r >> 2);
            out[x * 4 + 1] = (g << 3) | (g >> 2);
            out[x * 4 + 2] = (b << 3) | (b >> 2);
            out[x * 4 + 3] = (p & 0x8000) ? 255 : 0;
        }
    }
}

static void argb4444ToRGBA(const uint16_t* src, uint8_t* dst, int w, int h, int pitchBytes)
{
    for (int y = 0; y < h; y++)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(src) + y * pitchBytes);
        uint8_t* out = dst + y * w * 4;
        for (int x = 0; x < w; x++)
        {
            uint16_t p = row[x];
            uint8_t a = (p >> 12) & 0xF;
            uint8_t r = (p >> 8) & 0xF;
            uint8_t g = (p >> 4) & 0xF;
            uint8_t b = p & 0xF;
            out[x * 4 + 0] = (r << 4) | r;
            out[x * 4 + 1] = (g << 4) | g;
            out[x * 4 + 2] = (b << 4) | b;
            out[x * 4 + 3] = (a << 4) | a;
        }
    }
}

// --- DXT block decompression ---

static inline void expand565(uint16_t c, uint8_t out[4])
{
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    out[0] = (r << 3) | (r >> 2);
    out[1] = (g << 2) | (g >> 4);
    out[2] = (b << 3) | (b >> 2);
    out[3] = 255;
}

static void decodeDXT1Block(const uint8_t* block, uint8_t pixels[4][4][4], bool punchThrough)
{
    uint16_t c0 = block[0] | (block[1] << 8);
    uint16_t c1 = block[2] | (block[3] << 8);
    uint8_t colors[4][4];
    expand565(c0, colors[0]);
    expand565(c1, colors[1]);
    if (c0 > c1 || !punchThrough)
    {
        for (int i = 0; i < 3; i++)
        {
            colors[2][i] = (2 * colors[0][i] + colors[1][i] + 1) / 3;
            colors[3][i] = (colors[0][i] + 2 * colors[1][i] + 1) / 3;
        }
        colors[2][3] = colors[3][3] = 255;
    }
    else
    {
        for (int i = 0; i < 3; i++)
            colors[2][i] = (colors[0][i] + colors[1][i]) / 2;
        colors[2][3] = 255;
        colors[3][0] = colors[3][1] = colors[3][2] = 0;
        colors[3][3] = 0;
    }
    uint32_t idx = block[4] | (block[5] << 8) | (block[6] << 16) | ((uint32_t)block[7] << 24);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            std::memcpy(pixels[y][x], colors[(idx >> ((y * 4 + x) * 2)) & 3], 4);
}

static void writeDXTBlock(uint8_t* rgba, int imgW, int imgH, int bx, int by, const uint8_t pixels[4][4][4])
{
    for (int py = 0; py < 4 && by * 4 + py < imgH; py++)
        for (int px = 0; px < 4 && bx * 4 + px < imgW; px++)
            std::memcpy(&rgba[((by * 4 + py) * imgW + bx * 4 + px) * 4], pixels[py][px], 4);
}

static void decompressDXT1(uint8_t* rgba, const uint8_t* data, int w, int h)
{
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++)
        {
            uint8_t pixels[4][4][4];
            decodeDXT1Block(data, pixels, true);
            data += 8;
            writeDXTBlock(rgba, w, h, bx, by, pixels);
        }
}

static void decompressDXT3(uint8_t* rgba, const uint8_t* data, int w, int h)
{
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++)
        {
            uint8_t pixels[4][4][4];
            decodeDXT1Block(data + 8, pixels, false);
            for (int i = 0; i < 16; i++)
            {
                int nibble = (i & 1) ? (data[i / 2] >> 4) : (data[i / 2] & 0xF);
                pixels[i / 4][i % 4][3] = (nibble << 4) | nibble;
            }
            data += 16;
            writeDXTBlock(rgba, w, h, bx, by, pixels);
        }
}

static void decompressDXT5(uint8_t* rgba, const uint8_t* data, int w, int h)
{
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++)
        {
            uint8_t a0 = data[0], a1 = data[1];
            uint8_t alphas[8] = {a0, a1};
            if (a0 > a1)
                for (int i = 1; i <= 6; i++)
                    alphas[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
            else
            {
                for (int i = 1; i <= 4; i++)
                    alphas[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
                alphas[6] = 0;
                alphas[7] = 255;
            }
            uint64_t aBits = 0;
            for (int i = 0; i < 6; i++)
                aBits |= (uint64_t)data[2 + i] << (i * 8);

            uint8_t pixels[4][4][4];
            decodeDXT1Block(data + 8, pixels, false);
            for (int i = 0; i < 16; i++)
                pixels[i / 4][i % 4][3] = alphas[(aBits >> (i * 3)) & 7];

            data += 16;
            writeDXTBlock(rgba, w, h, bx, by, pixels);
        }
}

// --- Public API ---

bool ReadPAAInfo(const std::string& path, PAAInfo& info)
{
    info.path = path;
    std::ifstream f(path, std::ios::binary);
    if (!f.good())
        return false;

    info.isPaa = detectIsPaa(path);
    info.magic = readU16(f);
    info.formatName = fmtName(info.magic);
    if (!info.formatName)
    {
        f.seekg(0, std::ios::beg);
    }

    // Skip TAGG sections
    while (f.good())
    {
        uint32_t m = readU32(f);
        if (m != 0x54414747)
        {
            f.seekg(-4, std::ios::cur);
            break;
        }
        readU32(f);
        uint32_t sz = readU32(f);
        f.seekg(sz, std::ios::cur);
    }

    info.paletteColors = readU16(f);
    if (info.paletteColors > 0)
        f.seekg(info.paletteColors * 3, std::ios::cur);

    info.mipmapCount = 0;
    bool first = true;
    while (f.good())
    {
        uint16_t w = readU16(f);
        uint16_t h = readU16(f);
        if (w == 0 && h == 0)
            break;
        if (!f.good())
            break;
        int rw = w, rh = h;
        if (w == 1234 && h == 8765)
        {
            rw = readU16(f);
            rh = readU16(f);
        }
        if (first)
        {
            info.width = rw;
            info.height = rh;
            first = false;
        }
        info.mipmapCount++;
        uint32_t dataSize = 0;
        f.read(reinterpret_cast<char*>(&dataSize), 3);
        dataSize &= 0xFFFFFF;

        // DXT1: scan first mipmap for transparent blocks (c0 <= c1)
        if (info.mipmapCount == 1 && info.magic == 0xFF01 && dataSize >= 8)
        {
            auto pos = f.tellg();
            std::vector<uint8_t> blockData(dataSize);
            f.read(reinterpret_cast<char*>(blockData.data()), dataSize);
            // Each DXT1 block is 8 bytes: 2 bytes c0, 2 bytes c1, 4 bytes indices
            for (size_t off = 0; off + 8 <= dataSize; off += 8)
            {
                uint16_t c0 = blockData[off] | (blockData[off + 1] << 8);
                uint16_t c1 = blockData[off + 2] | (blockData[off + 3] << 8);
                if (c0 <= c1)
                {
                    info.hasTransparentBlocks = true;
                    break;
                }
            }
            // Already consumed, no need to seek past
            (void)pos;
        }
        else
        {
            f.seekg(dataSize, std::ios::cur);
        }
    }

    return info.width > 0 && info.height > 0;
}

namespace
{
// Decode one already-Init()'d mip level's pixel data into RGBA8888.
// Factored out of DecodePAABuffer so DecodePAABufferAllMips (which calls
// this once per level) and the single-level case decode identically instead
// of risking the two paths drifting apart. `mip` must already have `_w`/
// `_h`/`_start` populated (i.e. Init() returned 0 for it already) -- this
// function re-seeks to `_start` itself (SeekLevel) for the formats that read
// their own raw header, exactly mirroring the original inline logic's order.
bool DecodeLevelPixels(QIStream& in, PacFormat format, PacLevelMem& mip, PacPalette& pal, bool isPaa, int width,
                       int height, std::vector<uint8_t>& outRgba)
{
    outRgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    bool isDXT = (format >= PacDXT1 && format <= PacDXT5);

    if (format == PacARGB8888)
    {
        // ARGB8888: raw data, no LZSS. Convert ARGB → RGBA.
        mip.SeekLevel(in);
        int mw = fgetiw(in), mh = fgetiw(in);
        if (mw == 0 && mh == 0)
        {
            outRgba.clear();
            return false;
        }
        int dSize = fgeti24(in);
        std::vector<uint8_t> rawData(dSize);
        in.read(reinterpret_cast<char*>(rawData.data()), dSize);
        // The conversion loop below reads width*height*4 raw bytes; reject a payload
        // too small to cover them rather than read past rawData (heap over-read).
        if (static_cast<int>(rawData.size()) < width * height * 4)
        {
            outRgba.clear();
            return false;
        }
        for (int y = 0; y < height; y++)
        {
            const uint8_t* src = rawData.data() + y * width * 4;
            uint8_t* dst = outRgba.data() + y * width * 4;
            for (int x = 0; x < width; x++)
            {
                // LE memory: B(0) G(1) R(2) A(3) from uint32 (A<<24|R<<16|G<<8|B)
                dst[x * 4 + 0] = src[x * 4 + 2]; // R
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // B
                dst[x * 4 + 3] = src[x * 4 + 3]; // A
            }
        }
    }
    else if (isDXT)
    {
        mip.SeekLevel(in);
        int mw = fgetiw(in), mh = fgetiw(in);
        if (mw == 0 && mh == 0)
        {
            outRgba.clear();
            return false;
        }
        int dSize = fgeti24(in);
        std::vector<uint8_t> dxtData(dSize);
        in.read(reinterpret_cast<char*>(dxtData.data()), dSize);
        // The DXT decoders read blockBytes per 4x4 block across width*height; reject a
        // compressed payload too small to cover them rather than read past dxtData.
        int blockBytes = (format == PacDXT1) ? 8 : 16;
        int blocks = ((width + 3) / 4) * ((height + 3) / 4);
        if (static_cast<int>(dxtData.size()) < blocks * blockBytes)
        {
            outRgba.clear();
            return false;
        }
        switch (format)
        {
            case PacDXT1:
                decompressDXT1(outRgba.data(), dxtData.data(), width, height);
                break;
            case PacDXT2:
            case PacDXT3:
                decompressDXT3(outRgba.data(), dxtData.data(), width, height);
                break;
            case PacDXT4:
            case PacDXT5:
                decompressDXT5(outRgba.data(), dxtData.data(), width, height);
                break;
            default:
                outRgba.clear();
                return false;
        }
    }
    else if (format == PacARGB4444 || format == PacAI88)
    {
        mip.SetDestFormat(PacARGB4444, 4);
        std::vector<uint8_t> mipData(mip.Size(), 0);
        mip.SeekLevel(in);
        int ret = isPaa ? mip.LoadPaa(in, mipData.data(), &pal) : mip.LoadPac(in, mipData.data(), &pal);
        if (ret != 0)
        {
            outRgba.clear();
            return false;
        }
        argb4444ToRGBA(reinterpret_cast<const uint16_t*>(mipData.data()), outRgba.data(), width, height, mip.Pitch());
    }
    else
    {
        mip.SetDestFormat(PacARGB1555, 4);
        std::vector<uint8_t> mipData(mip.Size(), 0);
        mip.SeekLevel(in);
        int ret = isPaa ? mip.LoadPaa(in, mipData.data(), &pal) : mip.LoadPac(in, mipData.data(), &pal);
        if (ret != 0)
        {
            outRgba.clear();
            return false;
        }
        argb1555ToRGBA(reinterpret_cast<const uint16_t*>(mipData.data()), outRgba.data(), width, height, mip.Pitch());
    }
    return true;
}
} // namespace

DecodedImage DecodePAABuffer(const void* data, size_t size, bool isPaa)
{
    DecodedImage img;
    QIStream in(static_cast<const char*>(data), static_cast<int>(size));

    int desc = fgetiw(in);
    bool alpha = false;
    PacFormat format = PacFormatFromDesc(desc, alpha);
    if (format == PacFormatN)
    {
        in.seekg(-2, QIOS::cur);
        format = isPaa ? PacARGB4444 : PacP8;
    }

    PacPalette pal;
    int offsets[16];
    for (int i = 0; i < 16; i++)
        offsets[i] = -1;
    if (pal.Load(in, offsets, 16))
        return img;

    PacLevelMem mip;
    if (offsets[0] >= 0)
        mip.SetStart(offsets[0]);
    if (mip.Init(in, format) != 0)
        return img;

    img.width = mip._w;
    img.height = mip._h;
    img.hasAlphaChannel = alpha;
    img.oneBitAlpha = (format == PacDXT1);
    img.isChromaKey = pal._isTransparent;
    // Dimensions come from the mip header; reject absurd sizes before width*height*4
    // overflows int (a tiny rgba alloc the decoders then overrun) or drives a huge
    // allocation. PAA textures are far below this cap.
    if (img.width <= 0 || img.height <= 0 || img.width > 8192 || img.height > 8192)
    {
        return img;
    }

    if (!DecodeLevelPixels(in, format, mip, pal, isPaa, img.width, img.height, img.rgba))
        return img;

    return img;
}

DecodedImageChain DecodePAABufferAllMips(const void* data, size_t size, bool isPaa)
{
    DecodedImageChain chain;
    QIStream in(static_cast<const char*>(data), static_cast<int>(size));

    int desc = fgetiw(in);
    bool alpha = false;
    PacFormat format = PacFormatFromDesc(desc, alpha);
    if (format == PacFormatN)
    {
        in.seekg(-2, QIOS::cur);
        format = isPaa ? PacARGB4444 : PacP8;
    }

    PacPalette pal;
    int offsets[16];
    for (int i = 0; i < 16; i++)
        offsets[i] = -1;
    if (pal.Load(in, offsets, 16))
        return chain;

    chain.hasAlphaChannel = alpha;
    chain.oneBitAlpha = (format == PacDXT1);
    chain.isChromaKey = pal._isTransparent;

    // Pass 1: walk every level's header sequentially. Each Init() call reads
    // one level's w/h/dSize, records _start, then seeks past that level's
    // data -- landing exactly on the next level's header (PacLevelMem::Init,
    // Pactext.cpp). A fresh PacLevelMem per level (not reused) since Init()
    // short-circuits ("already initialized") once _w/_h are set.
    std::vector<PacLevelMem> mips;
    for (int m = 0; m < 16; m++)
    {
        PacLevelMem mip;
        if (m < 16 && offsets[m] >= 0)
            mip.SetStart(offsets[m]);
        int ret = mip.Init(in, format);
        if (ret != 0)
            break; // 0x0 terminator (ret>0) or a real read error (ret<0) -- either way, no more levels
        mips.push_back(mip);
    }
    if (mips.empty())
        return chain;

    // Pass 2: decode each level's pixels (same per-format logic as the
    // single-level case, see DecodeLevelPixels' doc comment).
    chain.levels.reserve(mips.size());
    for (PacLevelMem& mip : mips)
    {
        DecodedImage level;
        level.width = mip._w;
        level.height = mip._h;
        level.hasAlphaChannel = alpha;
        level.oneBitAlpha = chain.oneBitAlpha;
        level.isChromaKey = chain.isChromaKey;
        // Same absurd-size guard as DecodePAABuffer -- a corrupt mip header
        // shouldn't be able to drive a huge allocation in the loop below.
        if (level.width <= 0 || level.height <= 0 || level.width > 8192 || level.height > 8192)
            break;
        if (!DecodeLevelPixels(in, format, mip, pal, isPaa, level.width, level.height, level.rgba))
            break; // a level failing to decode invalidates every coarser level after it too
        chain.levels.push_back(std::move(level));
    }
    return chain;
}

DecodedImage DecodePAAFile(const std::string& path)
{
    return DecodePAAFileMip(path, 0);
}

DecodedImage DecodePAAFileMip(const std::string& path, int mipLevel)
{
    DecodedImage img;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.good())
        return img;

    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> fileData(fileSize);
    file.read(fileData.data(), fileSize);
    file.close();

    if (mipLevel == 0)
        return DecodePAABuffer(fileData.data(), static_cast<size_t>(fileSize), detectIsPaa(path));

    // For mip > 0, parse the PAA structure and skip to the requested mip
    bool isPaa = detectIsPaa(path);
    QIStream in(fileData.data(), static_cast<int>(fileSize));

    int desc = fgetiw(in);
    bool alpha = false;
    PacFormat format = PacFormatFromDesc(desc, alpha);
    if (format == PacFormatN)
    {
        in.seekg(-2, QIOS::cur);
        format = isPaa ? PacARGB4444 : PacP8;
    }

    PacPalette pal;
    int offsets[16];
    for (int i = 0; i < 16; i++)
        offsets[i] = -1;
    if (pal.Load(in, offsets, 16))
        return img;

    // Iterate through mip levels to reach the requested one
    PacLevelMem mip;
    for (int m = 0; m <= mipLevel; m++)
    {
        mip = PacLevelMem(); // fresh for each level
        if (m < 16 && offsets[m] >= 0)
            mip.SetStart(offsets[m]);
        // For mips > 0 with known offsets, set expected dimensions
        if (m > 0 && offsets[m] >= 0)
        {
            // will be read by Init anyway
        }
        if (mip.Init(in, format) != 0)
            return img;
    }

    img.width = mip._w;
    img.height = mip._h;
    img.hasAlphaChannel = alpha;
    img.oneBitAlpha = (format == PacDXT1);
    img.isChromaKey = pal._isTransparent;
    // Reject absurd dimensions (mip header is attacker-controlled) before
    // width*height*4 overflows int into a tiny rgba alloc the decoders overrun.
    if (img.width <= 0 || img.height <= 0 || img.width > 8192 || img.height > 8192)
        return img;
    img.rgba.resize(img.width * img.height * 4);

    bool isDXT = (format >= PacDXT1 && format <= PacDXT5);

    if (isDXT)
    {
        mip.SeekLevel(in);
        int mw = fgetiw(in), mh = fgetiw(in);
        if (mw == 0 && mh == 0)
        {
            img.rgba.clear();
            return img;
        }
        int dSize = fgeti24(in);
        std::vector<uint8_t> dxtData(dSize);
        in.read(reinterpret_cast<char*>(dxtData.data()), dSize);
        // The DXT decoders read blockBytes per 4x4 block across width*height; reject a
        // compressed payload too small to cover them rather than read past dxtData.
        int blockBytes = (format == PacDXT1) ? 8 : 16;
        int blocks = ((img.width + 3) / 4) * ((img.height + 3) / 4);
        if (static_cast<int>(dxtData.size()) < blocks * blockBytes)
        {
            img.rgba.clear();
            return img;
        }
        switch (format)
        {
            case PacDXT1:
                decompressDXT1(img.rgba.data(), dxtData.data(), img.width, img.height);
                break;
            case PacDXT2:
            case PacDXT3:
                decompressDXT3(img.rgba.data(), dxtData.data(), img.width, img.height);
                break;
            case PacDXT4:
            case PacDXT5:
                decompressDXT5(img.rgba.data(), dxtData.data(), img.width, img.height);
                break;
            default:
                img.rgba.clear();
                return img;
        }
    }
    else if (format == PacARGB4444 || format == PacAI88)
    {
        mip.SetDestFormat(PacARGB4444, 4);
        std::vector<uint8_t> mipData(mip.Size(), 0);
        mip.SeekLevel(in);
        int ret = isPaa ? mip.LoadPaa(in, mipData.data(), &pal) : mip.LoadPac(in, mipData.data(), &pal);
        if (ret != 0)
        {
            img.rgba.clear();
            return img;
        }
        argb4444ToRGBA(reinterpret_cast<const uint16_t*>(mipData.data()), img.rgba.data(), img.width, img.height,
                       mip.Pitch());
    }
    else
    {
        mip.SetDestFormat(PacARGB1555, 4);
        std::vector<uint8_t> mipData(mip.Size(), 0);
        mip.SeekLevel(in);
        int ret = isPaa ? mip.LoadPaa(in, mipData.data(), &pal) : mip.LoadPac(in, mipData.data(), &pal);
        if (ret != 0)
        {
            img.rgba.clear();
            return img;
        }
        argb1555ToRGBA(reinterpret_cast<const uint16_t*>(mipData.data()), img.rgba.data(), img.width, img.height,
                       mip.Pitch());
    }

    return img;
}

} // namespace Poseidon
