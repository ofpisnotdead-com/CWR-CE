#include "FontCommand.hpp"
#include "../SDLPreview.hpp"
#include <Poseidon/Asset/Formats/Common/CsvReader.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/Graphics/Textures/Image.hpp>
#include <Poseidon/Graphics/Textures/PAAEncoder.hpp>
#include <Poseidon/Graphics/Textures/PixelFormat.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontData.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontMapping.hpp>
#include <Poseidon/UI/Text/FontRenderer.hpp>
#include <Poseidon/UI/Text/ScreenTextLayout.hpp>
#include <Poseidon/Asset/Probes/AssetInfo.hpp>
#include <Poseidon/Asset/Probes/AssetPreview.hpp>
#include <cjson/cJSON.h>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <CLI/App.hpp>
#include <CLI/Error.hpp>
#include <CLI/Option.hpp>
#include <CLI/Validators.hpp>
#include <functional>
#include <string>
#include <vector>

namespace PoseidonTools
{

namespace
{
static void setupFontInspect(CLI::App& font)
{
    auto* cmd = font.add_subcommand("inspect", "Inspect FXY font file metadata");
    static std::string inputPath;
    cmd->add_option("input", inputPath, "Input FXY font file")->required()->check(CLI::ExistingFile);

    cmd->callback(
        []()
        {
            auto info = Poseidon::InspectFont(inputPath);
            if (!info.valid)
            {
                std::cerr << "Error: Failed to load font: " << inputPath << std::endl;
                throw CLI::RuntimeError(1);
            }

            std::cout << "File: " << inputPath << std::endl;
            std::cout << "Name: " << info.name << std::endl;
            std::cout << "Glyphs: " << info.glyphCount << std::endl;
            std::cout << "Max Height: " << info.maxHeight << " px" << std::endl;
            std::cout << "Max Width: " << info.maxWidth << " px" << std::endl;
            std::cout << "Texture Sets: " << info.textureSetCount << std::endl;
            if (info.charCodeMin >= 0)
                std::cout << "Char Range: " << info.charCodeMin << "-" << info.charCodeMax << std::endl;
            std::cout << "Textures:" << std::endl;
            for (size_t i = 0; i < info.textureNames.size(); i++)
            {
                std::cout << "  " << info.textureNames[i];
                if (i < info.texturesExist.size())
                    std::cout << (info.texturesExist[i] ? " (found)" : " (missing)");
                std::cout << std::endl;
            }
        });
}
static void setupFontShow(CLI::App& font)
{
    auto* cmd = font.add_subcommand("show", "Display font character map grid in a window");
    static std::string inputPath;
    static std::string screenshotPath;
    cmd->add_option("input", inputPath, "Input FXY font file")->required()->check(CLI::ExistingFile);
    cmd->add_option("--screenshot", screenshotPath, "Save screenshot to file and exit");

    cmd->callback(
        []()
        {
            Poseidon::FontPreviewOptions opts;
            opts.charmap = true;

            auto preview = Poseidon::PreviewFont(inputPath, opts);
            if (!preview.valid())
            {
                std::cerr << "Error: Failed to render font charmap" << std::endl;
                throw CLI::RuntimeError(1);
            }

            if (!screenshotPath.empty())
            {
                if (!preview.saveToFile(screenshotPath))
                {
                    std::cerr << "Error: Failed to write screenshot" << std::endl;
                    throw CLI::RuntimeError(1);
                }
                std::cout << "Screenshot: " << screenshotPath << " (" << preview.width << "x" << preview.height << ")"
                          << std::endl;
                return;
            }

            DisplayWindowRGBA("PoseidonTools - " + inputPath + " (charmap)", preview.width, preview.height,
                              preview.data.data());
        });
}
static void setupFontPreview(CLI::App& font)
{
    auto* cmd = font.add_subcommand("preview", "Preview font with custom text");
    static std::string inputPath;
    static std::string screenshotPath;
    static std::string sampleText;
    cmd->add_option("input", inputPath, "Input FXY font file")->required()->check(CLI::ExistingFile);
    cmd->add_option("--screenshot", screenshotPath, "Save screenshot to file and exit");
    cmd->add_option("--text", sampleText, "Custom sample text to render");

    cmd->callback(
        []()
        {
            Poseidon::FontPreviewOptions opts;
            if (!sampleText.empty())
                opts.sampleText = sampleText;

            auto preview = Poseidon::PreviewFont(inputPath, opts);
            if (!preview.valid())
            {
                std::cerr << "Error: Failed to render font preview" << std::endl;
                throw CLI::RuntimeError(1);
            }

            if (!screenshotPath.empty())
            {
                if (!preview.saveToFile(screenshotPath))
                {
                    std::cerr << "Error: Failed to write screenshot" << std::endl;
                    throw CLI::RuntimeError(1);
                }
                std::cout << "Screenshot: " << screenshotPath << " (" << preview.width << "x" << preview.height << ")"
                          << std::endl;
                return;
            }

            DisplayWindowRGBA("PoseidonTools - " + inputPath, preview.width, preview.height, preview.data.data());
        });
}
static void setupFontRender(CLI::App& font)
{
    auto* cmd = font.add_subcommand("render", "Render font to PNG file");
    static std::string inputPath;
    static std::string outputPath;
    static std::string sampleText;
    cmd->add_option("input", inputPath, "Input FXY font file")->required()->check(CLI::ExistingFile);
    cmd->add_option("-o,--output", outputPath, "Output image file (.png, .bmp, .tga)")->required();
    cmd->add_option("--text", sampleText, "Custom text (default: charmap grid)");

    cmd->callback(
        []()
        {
            Poseidon::FontPreviewOptions opts;
            if (!sampleText.empty())
                opts.sampleText = sampleText;
            else
                opts.charmap = true;

            auto preview = Poseidon::PreviewFont(inputPath, opts);
            if (!preview.valid())
            {
                std::cerr << "Error: Failed to render font" << std::endl;
                throw CLI::RuntimeError(1);
            }

            if (!preview.saveToFile(outputPath))
            {
                std::cerr << "Error: Failed to write output" << std::endl;
                throw CLI::RuntimeError(1);
            }

            std::cout << "Rendered: " << outputPath << " (" << preview.width << "x" << preview.height << ")"
                      << std::endl;
        });
}

// Mirror of Font.cpp's BucketFTPixelSize (not exported): snap an ideal pixel
// size to the 4px grid the FreeType atlas actually rasterizes at.
static int BucketFTPixelSizeTool(float idealPx)
{
    int bucketed = static_cast<int>((idealPx + 2.0f) / 4.0f) * 4;
    if (bucketed < 8)
        bucketed = 8;
    if (bucketed > 160)
        bucketed = 160;
    return bucketed;
}
// font textwidth — engine-exact rendered text width as a screen-width fraction
//
// Reproduces the FreeType branch of Engine::GetTextWidth (FontDraw.cpp): resolve
// the font alias through the active per-role slot mapping (FindFontMapping), load
// the mapped TTF, then apply the same widthScale / letterSpacing / synthetic
// bold-oblique and aspect/FOV scaling. Output is a fraction of screen width, so
// it compares directly against a control's `w`. Used to flag credit / cutscene
// text that overflows its bounds with the real shipping font.
static void setupFontTextWidth(CLI::App& font)
{
    auto* cmd = font.add_subcommand(
        "textwidth", "Engine-exact rendered text width (screen-width fraction) for a font alias + sizeEx");

    static std::string alias;
    static double sizeEx = 0.0;
    static std::string text;
    static std::string dataDir = "packages/Combined";
    static int width = 1920;
    static int height = 1080;
    static double fovTop = 0.75;
    static double fovLeft = 1.0;
    static double maxW = -1.0;
    static bool asJson = false;

    cmd->add_option("font", alias, "Font alias as used in a resource (e.g. SteelfishB64CE)")->required();
    cmd->add_option("sizeEx", sizeEx, "Control sizeEx (font height as a fraction of screen height)")->required();
    cmd->add_option("text", text, "Text to measure")->required();
    cmd->add_option("--data-dir", dataDir, "Game data dir holding fonts/ (default packages/Combined)");
    cmd->add_option("--width", width, "Surface width px (default 1920)");
    cmd->add_option("--height", height, "Surface height px (default 1080)");
    cmd->add_option("--fov-top", fovTop, "Aspect topFOV (config fovTop, default 0.75)");
    cmd->add_option("--fov-left", fovLeft, "Aspect leftFOV (config fovLeft, default 1.0)");
    cmd->add_option("--max-width", maxW, "If set, also report fits = width <= maxWidth (the control w)");
    cmd->add_flag("--json", asJson, "Emit a single-line JSON result");

    cmd->callback(
        []()
        {
            std::string low = alias;
            for (char& c : low)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            const Poseidon::FreeTypeFontMapping* m = Poseidon::FindFontMapping(low.c_str());
            if (!m)
            {
                std::cerr << "Error: no font mapping for alias '" << alias << "' (prefix not found)\n";
                throw CLI::RuntimeError(2);
            }

            std::string ttf = dataDir;
            if (!ttf.empty() && ttf.back() != '/' && ttf.back() != '\\')
                ttf += '/';
            for (const char* p = m->ttfPath; *p; ++p)
                ttf += (*p == '\\') ? '/' : *p;

            Poseidon::ui::FontRenderer fr;
            fr.SetSyntheticOblique(m->syntheticOblique);
            fr.SetSyntheticBold(m->syntheticBold);
            if (!fr.LoadFont(ttf))
            {
                std::cerr << "Error: cannot load font file: " << ttf << "\n";
                throw CLI::RuntimeError(1);
            }

            // Engine::GetTextWidth FreeType branch, headless: Width2D()/Height2D()
            // collapse to Width()/Height() over the full-screen UI region.
            const float legacyFontHeight = m->bitmapMaxHeight * (1.0f / 600.0f);
            Poseidon::ui::ScreenTextBaseScale base;
            if (!Poseidon::ui::ComputeScreenTextBaseScale(static_cast<float>(height), width, height,
                                                          static_cast<float>(fovTop), static_cast<float>(fovLeft),
                                                          legacyFontHeight, static_cast<float>(sizeEx), &base))
            {
                std::cerr << "Error: bad scale parameters\n";
                throw CLI::RuntimeError(3);
            }

            const int pixelSize = BucketFTPixelSizeTool(base.sizeH * static_cast<float>(m->renderPx));

            Poseidon::ui::ScreenTextScale scale;
            if (!Poseidon::ui::FinalizeFreeTypeScreenTextScale(base, m->renderPx, pixelSize, m->widthScale,
                                                               fr.GetAscent(pixelSize) + m->baselineOffset, &scale,
                                                               m->letterSpacing))
            {
                std::cerr << "Error: bad freetype scale\n";
                throw CLI::RuntimeError(3);
            }

            const float frac = Poseidon::ui::MeasureScreenTextWidth(fr, scale, static_cast<float>(width), text.c_str());

            if (asJson)
            {
                cJSON* o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "width", frac);
                cJSON_AddStringToObject(o, "font", alias.c_str());
                cJSON_AddStringToObject(o, "ttf", m->ttfPath);
                cJSON_AddNumberToObject(o, "sizeEx", sizeEx);
                cJSON_AddNumberToObject(o, "pixelSize", pixelSize);
                if (maxW >= 0)
                {
                    cJSON_AddNumberToObject(o, "maxWidth", maxW);
                    cJSON_AddBoolToObject(o, "fits", frac <= maxW ? 1 : 0);
                }
                char* s = cJSON_PrintUnformatted(o);
                std::cout << (s ? s : "{}") << "\n";
                cJSON_free(s);
                cJSON_Delete(o);
            }
            else
            {
                std::cout << "width=" << frac << " (font=" << alias << " -> " << m->ttfPath << " sizeEx=" << sizeEx
                          << " px=" << pixelSize << ")";
                if (maxW >= 0)
                    std::cout << "  " << (frac <= maxW ? "FITS" : "OVERFLOW") << " (maxWidth=" << maxW << ")";
                std::cout << "\n";
            }
        });
}

} // namespace
void FontCommand::Setup(CLI::App& app)
{
    auto* font = app.add_subcommand("font", "Font file operations (FXY)");
    font->require_subcommand(1);

    setupFontInspect(*font);
    setupFontShow(*font);
    setupFontPreview(*font);
    setupFontRender(*font);
    setupFontTextWidth(*font);
}

} // namespace PoseidonTools
