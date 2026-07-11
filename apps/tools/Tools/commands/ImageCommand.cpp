#include "ImageCommand.hpp"
#include "../SDLPreview.hpp"
#include <Poseidon/Graphics/Textures/Image.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Graphics/Textures/PAAEncoder.hpp>
#include <Poseidon/Graphics/Textures/PixelFormat.hpp>
#include <Poseidon/Graphics/Textures/ImageContainer.hpp>
#include <Poseidon/Asset/Probes/AssetInfo.hpp>
#include <Poseidon/Asset/Probes/AssetPreview.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cstring>
#include <ctype.h>
#include <stdint.h>
#include <CLI/App.hpp>
#include <CLI/Error.hpp>
#include <CLI/Option.hpp>
#include <CLI/Validators.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <tuple>
#include <utility>

namespace PoseidonTools
{

static void setupImageInspect(CLI::App& image)
{
    auto* cmd = image.add_subcommand("inspect", "Inspect PAA/PAC texture file");
    static std::string inputPath;
    cmd->add_option("input", inputPath, "Input PAA/PAC file path")->required()->check(CLI::ExistingFile);

    cmd->callback(
        []()
        {
            auto info = Poseidon::InspectTexture(inputPath);
            if (!info.valid)
            {
                std::cerr << "Error: Failed to read texture: " << inputPath << std::endl;
                throw CLI::RuntimeError(1);
            }

            std::cout << "File: " << info.path << std::endl;
            std::cout << "Type: " << info.typeName << std::endl;
            if (info.formatName != "P8")
                std::cout << "Format: " << info.formatName << " (0x" << std::hex << info.magic << std::dec << ")"
                          << std::endl;
            else
                std::cout << "Format: P8 (palette-based)" << std::endl;
            std::cout << "Dimensions: " << info.width << "x" << info.height << std::endl;
            std::cout << "Mipmaps: " << info.mipmapCount << std::endl;
            if (info.paletteColors > 0)
                std::cout << "Palette: " << info.paletteColors << " colors" << std::endl;
            if (info.magic == 0xFF01)
                std::cout << "Transparency: "
                          << (info.hasTransparentBlocks ? "yes (1-bit alpha blocks detected)" : "no") << std::endl;

            // Alpha classification (opaque / cutout / blend) — decode the top mip and
            // classify its alpha channel via the shared ClassifyAlpha (the same signal a
            // section-sort renderer uses): only a "blend" texture (partial-alpha texels)
            // must be deferred to the back-to-front pass; opaque and cutout occlude.
            Poseidon::DecodedImage img = Poseidon::DecodePAAFile(inputPath);
            if (img.valid())
            {
                std::cout << "Chroma-key (palette transparent index): " << (img.isChromaKey ? "yes" : "no")
                          << std::endl;
                const size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
                const Poseidon::AlphaStats a = Poseidon::ClassifyAlpha(img.rgba.data(), n);
                const char* route =
                    a.kind == Poseidon::AlphaStats::Blend    ? "back-to-front alpha pass, NO depth-write (see-through)"
                    : a.kind == Poseidon::AlphaStats::Cutout ? "opaque pass, depth-write, discard holes (occludes)"
                                                             : "opaque pass, depth-write (occludes)";
                std::cout << std::fixed << std::setprecision(1);
                std::cout << "Alpha: min=" << a.aMin << " max=" << a.aMax << " mean=" << a.aMean
                          << "  (clear a=0: " << a.pctClear << "%  opaque a=255: " << a.pctOpaque
                          << "%  partial 0<a<255: " << a.pctPartial << "%)" << std::endl;
                std::cout << "Classification: " << Poseidon::AlphaKindName(a.kind) << std::endl;
                std::cout << "  -> route: " << route << std::endl;
            }

            // Full mip chain (every level the file actually stores) -- the
            // buffer-based decode the Metal renderer's texture loader uses
            // for real GPU mip chains (engine/Poseidon/Graphics/Textures/
            // PAADecoder.cpp's DecodePAABufferAllMips), surfaced here so a
            // mismatch against the header's `Mipmaps:` count is visible.
            std::ifstream f(inputPath, std::ios::binary | std::ios::ate);
            if (f.good())
            {
                const auto fileSize = f.tellg();
                f.seekg(0, std::ios::beg);
                std::vector<char> fileData(static_cast<size_t>(fileSize));
                f.read(fileData.data(), fileSize);
                const bool isPaaExt = inputPath.size() >= 4 &&
                                     (inputPath.back() == 'a' || inputPath.back() == 'A');
                const Poseidon::DecodedImageChain chain =
                    Poseidon::DecodePAABufferAllMips(fileData.data(), fileData.size(), isPaaExt);
                std::cout << "Mip chain (decoded): " << chain.levels.size() << " level(s)";
                for (const auto& lvl : chain.levels)
                    std::cout << " " << lvl.width << "x" << lvl.height;
                std::cout << std::endl;
            }
        });
}

static void setupImageConvert(CLI::App& image)
{
    auto* cmd = image.add_subcommand("convert", "Convert image between formats");
    static std::string inputPath;
    static std::string outputPath;
    static std::string formatStr;
    cmd->add_option("input", inputPath, "Input file (PAA/PAC/PNG/BMP/TGA/JPG)")->required()->check(CLI::ExistingFile);
    cmd->add_option("output", outputPath, "Output file (PAA/PNG/BMP/TGA)")->required();
    cmd->add_option("-f,--format", formatStr, "Pixel format for PAA output (e.g., DXT1, DXT5, ARGB4444)");

    cmd->callback(
        []()
        {
            auto img = Poseidon::Image::FromFile(inputPath);
            if (!img.valid())
            {
                std::cerr << "Error: Failed to load: " << inputPath << std::endl;
                throw CLI::RuntimeError(1);
            }

            auto outExt = outputPath.substr(outputPath.find_last_of('.'));
            std::string outExtLower = outExt;
            std::transform(outExtLower.begin(), outExtLower.end(), outExtLower.begin(), ::tolower);
            auto* ci = Poseidon::ImageContainerRegistry::FindByExtension(outExtLower.c_str());
            if (!ci)
            {
                std::cerr << "Error: Unsupported output format: " << outExt << std::endl;
                throw CLI::RuntimeError(1);
            }

            if (ci->container == Poseidon::ImageContainer::PAA)
            {
                Poseidon::PixelFormat fmt = Poseidon::PixelFormat::DXT5;
                if (!formatStr.empty())
                {
                    auto* fi = Poseidon::PixelFormatRegistry::FindByName(formatStr.c_str());
                    if (!fi)
                    {
                        std::cerr << "Error: Unknown pixel format: " << formatStr << std::endl;
                        std::cerr << "Use 'image formats' to list available formats." << std::endl;
                        throw CLI::RuntimeError(1);
                    }
                    if (fi->paaMagic == 0)
                    {
                        std::cerr << "Error: " << fi->name << " is not supported as PAA pixel format" << std::endl;
                        throw CLI::RuntimeError(1);
                    }
                    fmt = fi->format;
                }

                if (!Poseidon::PAAEncoder::WritePAA(outputPath, img, fmt))
                {
                    std::cerr << "Error: Failed to write PAA: " << outputPath << std::endl;
                    throw CLI::RuntimeError(1);
                }
                const auto& fmtInfo = Poseidon::PixelFormatRegistry::Get(fmt);
                std::cout << "Converted: " << inputPath << " -> " << outputPath << " (" << fmtInfo.name << ")"
                          << std::endl;
            }
            else
            {
                if (!img.Save(outputPath, ci->container))
                {
                    std::cerr << "Error: Failed to write: " << outputPath << std::endl;
                    throw CLI::RuntimeError(1);
                }
                std::cout << "Converted: " << inputPath << " -> " << outputPath << std::endl;
            }
        });
}

static void setupImageShow(CLI::App& image)
{
    auto* cmd = image.add_subcommand("show", "Display texture in a window");
    static std::string inputPath;
    static std::string screenshotPath;
    cmd->add_option("input", inputPath, "Input file (PAA/PAC/PNG/BMP/TGA/JPG)")->required()->check(CLI::ExistingFile);
    cmd->add_option("--screenshot", screenshotPath, "Save screenshot to file and exit");

    cmd->callback(
        []()
        {
            auto img = Poseidon::Image::FromFile(inputPath);
            if (!img.valid())
            {
                std::cerr << "Error: Failed to load texture" << std::endl;
                throw CLI::RuntimeError(1);
            }

            auto rgba = img.ToRGBA();
            if (!rgba.valid())
            {
                std::cerr << "Error: Failed to convert to RGBA" << std::endl;
                throw CLI::RuntimeError(1);
            }

            if (!screenshotPath.empty())
            {
                Poseidon::PreviewImage preview;
                preview.width = rgba.width();
                preview.height = rgba.height();
                preview.data.assign(rgba.data().begin(), rgba.data().end());
                if (!preview.saveToFile(screenshotPath))
                {
                    std::cerr << "Error: Failed to write screenshot" << std::endl;
                    throw CLI::RuntimeError(1);
                }
                std::cout << "Screenshot: " << screenshotPath << " (" << rgba.width() << "x" << rgba.height() << ")"
                          << std::endl;
                return;
            }

            char title[256];
            std::snprintf(title, sizeof(title), "PoseidonTools - %s (%dx%d)", inputPath.c_str(), rgba.width(),
                          rgba.height());
            DisplayWindowRGBA(title, rgba.width(), rgba.height(), rgba.data().data());
        });
}

static void setupImageFormats(CLI::App& image)
{
    auto* cmd = image.add_subcommand("formats", "List supported pixel formats and containers");

    cmd->callback(
        []()
        {
            std::cout << "Pixel Formats:" << std::endl;
            std::cout << std::endl;
            std::cout << "  ";
            std::cout << std::left << std::setw(12) << "Name";
            std::cout << std::right << std::setw(4) << "BPP" << "  ";
            std::cout << std::left << std::setw(14) << "Type";
            std::cout << std::left << std::setw(8) << "Alpha";
            std::cout << std::left << std::setw(5) << "PAA";
            std::cout << "Description" << std::endl;
            std::cout << "  " << std::string(82, '-') << std::endl;

            auto count = Poseidon::PixelFormatRegistry::AllFormatsCount();
            auto* formats = Poseidon::PixelFormatRegistry::AllFormats();
            for (size_t i = 0; i < count; ++i)
            {
                const auto& f = formats[i];
                std::cout << "  ";
                std::cout << std::left << std::setw(12) << f.name;
                std::cout << std::right << std::setw(4) << f.bitsPerPixel << "  ";
                std::cout << std::left << std::setw(14) << (f.isCompressed ? "compressed" : "uncompressed");
                std::cout << std::left << std::setw(8) << (f.hasAlpha ? "yes" : "-");
                std::cout << std::left << std::setw(5) << (f.paaMagic ? "yes" : "-");
                std::cout << f.description << std::endl;
            }

            std::cout << std::endl;
            std::cout << "Containers:" << std::endl;
            std::cout << std::endl;
            std::cout << "  ";
            std::cout << std::left << std::setw(8) << "Ext";
            std::cout << std::left << std::setw(6) << "Name";
            std::cout << std::left << std::setw(7) << "Read";
            std::cout << std::left << std::setw(7) << "Write";
            std::cout << "Description" << std::endl;
            std::cout << "  " << std::string(82, '-') << std::endl;

            int cCount = Poseidon::ImageContainerRegistry::ContainerCount();
            auto* containers = Poseidon::ImageContainerRegistry::AllContainers();
            for (int i = 0; i < cCount; ++i)
            {
                const auto& c = containers[i];
                std::cout << "  ";
                std::cout << std::left << std::setw(8) << c.extension;
                std::cout << std::left << std::setw(6) << c.name;
                std::cout << std::left << std::setw(7) << (c.canRead ? "yes" : "-");
                std::cout << std::left << std::setw(7) << (c.canWrite ? "yes" : "-");
                std::cout << c.description << std::endl;
            }

            std::cout << std::endl;
            std::cout << "Notes:" << std::endl;
            std::cout << "  All conversions go through RGBA8888 as intermediate format." << std::endl;
            std::cout << "  PAA column marks formats with PAA/PAC container support." << std::endl;
            std::cout << "  PNG/BMP always use RGBA8888 (converted automatically)." << std::endl;
            std::cout << "  DXT1 1-bit alpha is automatic: pixels with alpha < 128 become fully transparent."
                      << std::endl;
        });
}

static void setupImageCompare(CLI::App& image)
{
    auto* cmd = image.add_subcommand("compare", "Compare two images pixel-by-pixel");
    static std::string inputPath1;
    static std::string inputPath2;
    static std::string diffOutput;
    static double threshold = 0.0;
    cmd->add_option("image-a", inputPath1, "First image (PAA/PAC/PNG/BMP/TGA/JPG)")
        ->required()
        ->check(CLI::ExistingFile);
    cmd->add_option("image-b", inputPath2, "Second image (PAA/PAC/PNG/BMP/TGA/JPG)")
        ->required()
        ->check(CLI::ExistingFile);
    cmd->add_option("-o,--diff", diffOutput, "Save difference visualization to file (PNG/BMP/TGA)");
    cmd->add_option("-t,--threshold", threshold, "Max allowed mean diff (exit 0 if within, 1 if exceeded)");

    cmd->callback(
        []()
        {
            auto imgA = Poseidon::Image::FromFile(inputPath1);
            if (!imgA.valid())
            {
                std::cerr << "Error: Failed to load: " << inputPath1 << std::endl;
                throw CLI::RuntimeError(2);
            }

            auto imgB = Poseidon::Image::FromFile(inputPath2);
            if (!imgB.valid())
            {
                std::cerr << "Error: Failed to load: " << inputPath2 << std::endl;
                throw CLI::RuntimeError(2);
            }

            if (imgA.width() != imgB.width() || imgA.height() != imgB.height())
            {
                std::cout << "result: different" << std::endl;
                std::cout << "reason: size mismatch (" << imgA.width() << "x" << imgA.height() << " vs " << imgB.width()
                          << "x" << imgB.height() << ")" << std::endl;
                throw CLI::RuntimeError(1);
            }

            auto rgbaA = imgA.ToRGBA();
            auto rgbaB = imgB.ToRGBA();
            const auto& pixA = rgbaA.data();
            const auto& pixB = rgbaB.data();
            int w = rgbaA.width();
            int h = rgbaA.height();
            int pixelCount = w * h;
            uint64_t totalDiff = 0;
            int changedPixels = 0;
            int maxDiff = 0;
            std::vector<uint8_t> diffPixels;
            bool writeDiff = !diffOutput.empty();
            if (writeDiff)
                diffPixels.resize(pixelCount * 4);

            for (int i = 0; i < pixelCount; ++i)
            {
                int base = i * 4;
                int dr = std::abs(static_cast<int>(pixA[base + 0]) - static_cast<int>(pixB[base + 0]));
                int dg = std::abs(static_cast<int>(pixA[base + 1]) - static_cast<int>(pixB[base + 1]));
                int db = std::abs(static_cast<int>(pixA[base + 2]) - static_cast<int>(pixB[base + 2]));
                int pixMax = std::max({dr, dg, db});

                totalDiff += dr + dg + db;
                if (pixMax > maxDiff)
                    maxDiff = pixMax;
                if (pixMax > 0)
                    changedPixels++;

                if (writeDiff)
                {
                    diffPixels[base + 0] = static_cast<uint8_t>(std::min(dr * 10, 255));
                    diffPixels[base + 1] = static_cast<uint8_t>(std::min(dg * 10, 255));
                    diffPixels[base + 2] = static_cast<uint8_t>(std::min(db * 10, 255));
                    diffPixels[base + 3] = 255;
                }
            }

            double meanDiff = static_cast<double>(totalDiff) / (pixelCount * 3);
            bool identical = (totalDiff == 0);

            std::cout << "result: " << (identical ? "identical" : "different") << std::endl;
            std::cout << "dimensions: " << w << "x" << h << std::endl;
            std::cout << std::fixed << std::setprecision(4);
            std::cout << "mean_diff: " << meanDiff << std::endl;
            std::cout << "max_diff: " << maxDiff << std::endl;
            std::cout << "changed_pixels: " << changedPixels << "/" << pixelCount << std::endl;

            if (writeDiff)
            {
                auto diffImg = Poseidon::Image::FromRGBA(w, h, std::move(diffPixels));
                if (!diffImg.Save(diffOutput))
                {
                    std::cerr << "Error: Failed to write diff image: " << diffOutput << std::endl;
                    throw CLI::RuntimeError(2);
                }
                std::cout << "diff_image: " << diffOutput << std::endl;
            }

            if (!identical && threshold > 0.0 && meanDiff > threshold)
                throw CLI::RuntimeError(1);
            if (!identical && threshold <= 0.0)
                throw CLI::RuntimeError(1);
        });
}

// Score how strongly an image looks like a black+white nested frame/border: a border ring
// carrying BOTH near-black and near-white pixels (the photo-mat look), with the center
// noticeably less black-and-white than the ring. Returns {score, ringBlackFrac, ringWhiteFrac}.
static std::tuple<double, double, double> FrameBorderScore(const Poseidon::DecodedImage& img)
{
    const int w = img.width, h = img.height;
    if (w < 8 || h < 8)
        return {0.0, 0.0, 0.0};
    const uint8_t* p = img.rgba.data();
    auto luma = [&](int x, int y)
    {
        const uint8_t* q = p + (static_cast<size_t>(y) * w + x) * 4;
        return (q[0] * 299 + q[1] * 587 + q[2] * 114) / 1000;
    };
    const int bw = std::max(1, w / 8), bh = std::max(1, h / 8); // ~12.5% border ring
    long ringN = 0, ringBlack = 0, ringWhite = 0, ctrN = 0, ctrBW = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const int L = luma(x, y);
            const bool border = (x < bw || x >= w - bw || y < bh || y >= h - bh);
            if (border)
            {
                ringN++;
                if (L < 50)
                    ringBlack++;
                else if (L > 205)
                    ringWhite++;
            }
            else
            {
                ctrN++;
                if (L < 50 || L > 205)
                    ctrBW++;
            }
        }
    const double rb = ringN ? double(ringBlack) / ringN : 0.0;
    const double rw = ringN ? double(ringWhite) / ringN : 0.0;
    const double cbw = ctrN ? double(ctrBW) / ctrN : 0.0;
    // Both black and white must populate the ring; reward a center that is less extreme.
    const double score = std::min(rb, rw) * (1.0 - 0.5 * cbw);
    return {score, rb, rw};
}

static void setupImageScan(CLI::App& image)
{
    auto* cmd = image.add_subcommand("scan", "Scan a directory of textures for a black+white frame/border pattern");
    static std::string dir;
    static int top = 25;
    cmd->add_option("dir", dir, "Directory of textures (recursive; .paa/.pac)")
        ->required()
        ->check(CLI::ExistingDirectory);
    cmd->add_option("--top", top, "Show this many top matches (default 25)");
    cmd->callback(
        []()
        {
            namespace fs = std::filesystem;
            struct Hit
            {
                std::string path;
                int w, h;
                double score, rb, rw;
            };
            std::vector<Hit> hits;
            int scanned = 0;
            for (const auto& e : fs::recursive_directory_iterator(dir))
            {
                if (!e.is_regular_file())
                    continue;
                std::string ext = e.path().extension().string();
                for (auto& c : ext)
                    c = (char)tolower((unsigned char)c);
                if (ext != ".paa" && ext != ".pac")
                    continue;
                Poseidon::DecodedImage img = Poseidon::DecodePAAFile(e.path().string());
                if (!img.valid())
                    continue;
                scanned++;
                auto [score, rb, rw] = FrameBorderScore(img);
                if (score > 0.02)
                    hits.push_back({e.path().string(), img.width, img.height, score, rb, rw});
            }
            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
            std::cout << "Scanned " << scanned << " textures; " << hits.size() << " with a black+white border.\n\n";
            std::cout << std::left << std::setw(10) << "score" << std::setw(8) << "black" << std::setw(8) << "white"
                      << std::setw(11) << "size" << "texture\n";
            std::cout << std::string(90, '-') << "\n";
            int n = 0;
            for (const auto& hit : hits)
            {
                if (n++ >= top)
                    break;
                char sz[24];
                std::snprintf(sz, sizeof(sz), "%dx%d", hit.w, hit.h);
                std::cout << std::left << std::fixed << std::setprecision(3) << std::setw(10) << hit.score
                          << std::setw(8) << hit.rb << std::setw(8) << hit.rw << std::setw(11) << sz << hit.path
                          << "\n";
            }
        });
}

void ImageCommand::Setup(CLI::App& app)
{
    auto* image = app.add_subcommand("image", "Texture operations (PAA/PAC/PNG/BMP)");
    image->require_subcommand(1);

    setupImageInspect(*image);
    setupImageConvert(*image);
    setupImageShow(*image);
    setupImageFormats(*image);
    setupImageCompare(*image);
    setupImageScan(*image);
}

} // namespace PoseidonTools
