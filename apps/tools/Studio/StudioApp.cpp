#include "StudioApp.hpp"
#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Asset/Probes/AssetInfo.hpp>
#include <Poseidon/Asset/Probes/AssetPreview.hpp>
#include <Poseidon/Asset/Probes/SoundPlayer.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontData.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <ctype.h>
#include <stdio.h>
#include <compare>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <system_error>
#include <utility>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{

static std::string StripPboExtension(const std::string& path);

StudioApp::~StudioApp()
{
    if (scanThread.joinable())
        scanThread.join();
    usedByCancelFlag = true;
    if (usedByThread.joinable())
        usedByThread.join();
    stopSound();
    clearPreviewTexture();
    clearGridThumbs();
    if (!tempExtractedFile.empty())
        std::filesystem::remove(tempExtractedFile);
    QIFStreamB::ClearBanks();
    GUseFileBanks = false;
}

void StudioApp::clearPreviewTexture()
{
    if (previewTexture)
    {
        SDL_DestroyTexture(previewTexture);
        previewTexture = nullptr;
    }
    previewTexW = 0;
    previewTexH = 0;
    previewTextureFile.clear();
}

StudioApp::PreviewType StudioApp::previewTypeFromExtension(const std::string& ext)
{
    if (ext == ".paa" || ext == ".pac" || ext == ".png" || ext == ".bmp" || ext == ".tga" || ext == ".jpg")
        return PreviewType::Image;
    if (ext == ".p3d")
        return PreviewType::Model;
    if (ext == ".wav" || ext == ".wss" || ext == ".ogg")
        return PreviewType::Sound;
    if (ext == ".wrp")
        return PreviewType::Terrain;
    if (ext == ".pbo")
        return PreviewType::Pbo;
    if (ext == ".fxy")
        return PreviewType::Font;
    if (ext == ".cpp" || ext == ".bin" || ext == ".hpp")
        return PreviewType::Config;
    return PreviewType::Unknown;
}

void StudioApp::selectFileAt(int index)
{
    if (index < 0 || index >= (int)filteredFiles.size())
        return;
    selectedIndex = index;
    const auto& f = filteredFiles[index];
    selectedFile = f.name;
    selectedEntry = f;
    currentPreview = PreviewType::None;
    previewInfo.clear();
    clearPreviewTexture();
    stopSound();
    cachedInfoFile.clear();
    cachedInfoText.clear();
    modelPreviewKey.clear();
    textureZoom = 1.0f;
    texturePanOffset = {0, 0};
    texturePanning = false;
    textureMipLevel = 0;
    modelLodIndex = 0;
    modelView = "front";
    assetUses.clear();
    if (usedByScanKey != f.name)
    {
        std::lock_guard<std::mutex> lk(usedByMutex);
        assetUsedBy.clear();
        usedByScanKey.clear();
    }
    configFile.reset();
    configFilePath.clear();
    configSearchActive = false;
    memset(configSearch, 0, sizeof(configSearch));
    configSearchResults.clear();

    std::string ext = f.extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    currentPreview = previewTypeFromExtension(ext);

    log("Selected: " + f.name);
}

void StudioApp::selectSingleFile(const std::string& absPath)
{
    namespace fs = std::filesystem;
    FileEntry f;
    f.fullPath = absPath;
    f.name = fs::path(absPath).filename().string();
    f.extension = fs::path(absPath).extension().string();
    std::transform(f.extension.begin(), f.extension.end(), f.extension.begin(), ::tolower);
    std::error_code ec;
    f.size = (int64_t)fs::file_size(absPath, ec);

    selectedFile = f.name;
    selectedEntry = f;
    selectedIndex = -1;
    previewInfo.clear();
    clearPreviewTexture();
    stopSound();
    cachedInfoFile.clear();
    cachedInfoText.clear();
    configFile.reset();
    configFilePath.clear();
    configSearchActive = false;
    memset(configSearch, 0, sizeof(configSearch));
    configSearchResults.clear();
    currentPreview = previewTypeFromExtension(f.extension);
    singleFileMode = true;

    log("Single-file: " + f.name);
}

void StudioApp::playSoundFile(const std::string& path)
{
    stopSound();
    if (!soundPlayer)
        soundPlayer = std::make_unique<SoundPlayer>();

    if (!soundPlayer->play(path.c_str()))
    {
        log("Audio: Cannot load: " + path);
        return;
    }

    soundPlaying = true;
    soundStartTime = std::chrono::steady_clock::now();
    soundPlayingFile = path;
    log("Playing: " + path + " (" + std::to_string(soundPlayer->duration()) + "s)");
}

void StudioApp::stopSound()
{
    if (soundPlayer)
        soundPlayer->stop();
    soundPlaying = false;
    soundPlayingFile.clear();
}

bool StudioApp::shouldShowGrid() const
{
    if (gridDetailMode)
        return false;
    return selectedIndices.size() != 1;
}

void StudioApp::playSoundEntry(const FileEntry& entry)
{
    if (entry.isVirtual())
    {
        try
        {
            QFBank bank;
            std::string bankName = StripPboExtension(entry.pboPath);
            if (!bank.open(RString(bankName.c_str())))
            {
                log("Audio: Failed to open PBO: " + entry.pboPath);
                return;
            }
            bank.Lock();
            if (bank.error())
            {
                log("Audio: PBO load error: " + entry.pboPath);
                return;
            }
            Ref<IFileBuffer> data = bank.Read(entry.pboFilename.c_str());
            if (!data || data->GetSize() == 0)
            {
                log("Audio: Failed to read from PBO: " + entry.pboFilename);
                return;
            }

            stopSound();
            if (!soundPlayer)
                soundPlayer = std::make_unique<SoundPlayer>();

            if (!soundPlayer->playMemory(data->GetData(), data->GetSize(), entry.extension.c_str()))
            {
                log("Audio: Cannot load: " + entry.pboFilename);
                return;
            }
            soundPlaying = true;
            soundStartTime = std::chrono::steady_clock::now();
            soundPlayingFile = entry.pboFilename;
            log("Playing: " + entry.pboFilename + " (" + std::to_string(soundPlayer->duration()) + "s)");
        }
        catch (const std::exception& e)
        {
            log("Audio: " + std::string(e.what()));
        }
    }
    else
    {
        playSoundFile(entry.fullPath);
    }
}

static std::string StripPboExtension(const std::string& path)
{
    auto pos = path.rfind('.');
    if (pos != std::string::npos)
    {
        std::string ext = path.substr(pos);
        std::string extLower = ext;
        std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
        if (extLower == ".pbo")
            return path.substr(0, pos);
    }
    return path;
}

static std::atomic<int> gExtractCounter{0};

std::string StudioApp::extractPboFile(const std::string& pboPath, const std::string& pboFilename)
{
    try
    {
        QFBank bank;
        std::string bankName = StripPboExtension(pboPath);
        if (!bank.open(RString(bankName.c_str())))
            return {};
        bank.Lock();
        if (bank.error())
            return {};
        Ref<IFileBuffer> data = bank.Read(pboFilename.c_str());
        if (!data || data->GetSize() == 0)
            return {};
        auto tempDir = std::filesystem::temp_directory_path();
        auto ext = std::filesystem::path(pboFilename).extension().string();
        int id = gExtractCounter++;
        std::string tmpPath = (tempDir / ("poseidon_xref_" + std::to_string(id) + ext)).string();
        std::ofstream out(tmpPath, std::ios::binary);
        if (!out)
            return {};
        out.write(static_cast<const char*>(data->GetData()), data->GetSize());
        out.close();
        return tmpPath;
    }
    catch (...)
    {
        return {};
    }
}

std::string StudioApp::extractVirtualFile(const FileEntry& entry)
{
    if (!entry.isVirtual())
        return entry.fullPath;
    if (!tempExtractedFile.empty())
    {
        std::error_code ec;
        std::filesystem::remove(tempExtractedFile, ec);
        tempExtractedFile.clear();
    }

    try
    {
        QFBank bank;
        std::string bankName = StripPboExtension(entry.pboPath);
        if (!bank.open(RString(bankName.c_str())))
        {
            log("Failed to open PBO: " + entry.pboPath);
            return {};
        }
        bank.Lock();
        if (bank.error())
        {
            log("PBO load error: " + entry.pboPath);
            return {};
        }

        Ref<IFileBuffer> data = bank.Read(entry.pboFilename.c_str());
        if (!data || data->GetSize() == 0)
        {
            log("Failed to read from PBO: " + entry.pboFilename);
            return {};
        }
        auto tempDir = std::filesystem::temp_directory_path();
        std::string ext = entry.extension;
        tempExtractedFile = (tempDir / ("poseidon_studio_tmp" + ext)).string();
        std::ofstream out(tempExtractedFile, std::ios::binary);
        out.write(static_cast<const char*>(data->GetData()), data->GetSize());
        out.close();

        return tempExtractedFile;
    }
    catch (const std::exception& e)
    {
        log("Extract error: " + std::string(e.what()));
        return {};
    }
}

std::string StudioApp::resolveFilePath(const FileEntry& entry)
{
    if (entry.isVirtual())
        return extractVirtualFile(entry);
    return entry.fullPath;
}

static std::string normalizePath(const std::string& p)
{
    std::string s = p;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (!s.empty() && s[0] == '/')
        s = s.substr(1);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string resolveVfsDisplayPath(const std::string& enginePath)
{
    if (enginePath.empty() || !GUseFileBanks)
        return {};
    std::string epath = enginePath;
    std::replace(epath.begin(), epath.end(), '/', '\\');
    if (!epath.empty() && epath[0] == '\\')
        epath = epath.substr(1);

    QFBank* bank = QIFStreamB::AutoBank(epath.c_str());
    if (!bank)
        return {};
    RString prefix = bank->GetPrefix();
    const char* remainder = epath.c_str() + prefix.GetLength();
    if (!bank->FileExists(remainder))
        return {};
    RString openName = bank->GetOpenName();
    std::filesystem::path pboFsPath((const char*)openName);
    std::string pboFilename = pboFsPath.filename().string();
    std::string parentDir = pboFsPath.parent_path().filename().string();
    std::string display = parentDir + "/" + pboFilename + "/" + std::string(remainder);
    std::replace(display.begin(), display.end(), '\\', '/');
    return display;
}

int StudioApp::findRefInFiltered(const std::string& refName) const
{
    std::string norm = normalizePath(refName);
    if (norm.empty())
        return -1;
    for (int i = 0; i < (int)filteredFiles.size(); i++)
    {
        std::string fname = normalizePath(filteredFiles[i].name);
        if (fname == norm)
            return i;
        if (fname.size() > norm.size() && fname[fname.size() - norm.size() - 1] == '/' &&
            fname.compare(fname.size() - norm.size(), norm.size(), norm) == 0)
            return i;
        if (norm.size() > fname.size() && norm[norm.size() - fname.size() - 1] == '/' &&
            norm.compare(norm.size() - fname.size(), fname.size(), fname) == 0)
            return i;
    }
    return -1;
}

void StudioApp::selectFoundRefs(const std::vector<AssetRef>& refs)
{
    searchFilter.clear();
    activeCategory = FileCategory::All;
    applyFilter();

    selectedIndices.clear();
    gridDetailMode = false;
    for (const auto& r : refs)
    {
        if (!r.exists)
            continue;
        int idx = -1;
        if (!r.vfsPath.empty())
            idx = findRefInFiltered(r.vfsPath);
        if (idx < 0)
            idx = findRefInFiltered(r.name);
        if (idx >= 0)
            selectedIndices.insert(idx);
    }
    if (selectedIndices.size() == 1)
        selectFileAt(*selectedIndices.begin());
    else if (!selectedIndices.empty())
    {
        selectedIndex = *selectedIndices.begin();
        selectedFile = filteredFiles[selectedIndex].name;
    }
}

void StudioApp::startUsedByScan()
{
    if (usedByScanning)
        return;

    usedByCancelFlag = true;
    if (usedByThread.joinable())
        usedByThread.join();
    usedByCancelFlag = false;

    std::string texName;
    if (selectedEntry.isVirtual())
        texName = normalizePath(selectedEntry.pboFilename);
    else
        texName = normalizePath(selectedEntry.name);
    if (texName.empty())
        return;

    std::string texBasename = texName;
    auto lastSlash = texBasename.rfind('/');
    if (lastSlash != std::string::npos)
        texBasename = texBasename.substr(lastSlash + 1);

    std::string selExt = selectedEntry.extension;
    std::transform(selExt.begin(), selExt.end(), selExt.begin(), ::tolower);
    bool scanModels =
        (selExt == ".paa" || selExt == ".pac" || selExt == ".tga" || selExt == ".jpg" || selExt == ".png");
    bool scanFonts = scanModels;

    usedByScanKey = selectedFile;
    usedByScanning = true;
    usedByScanDone = false;
    usedByScanProgress = 0;
    {
        std::lock_guard<std::mutex> lk(usedByMutex);
        assetUsedBy.clear();
    }

    // Engine types (QFBank, RString, Ref<>) are NOT thread-safe, so all PBO
    // extraction must happen here on the main thread before spawning the scan.
    struct ScanItem
    {
        std::string name;
        std::string inspectPath;
        std::string extension;
        bool needsCleanup;
    };
    std::vector<ScanItem> scanItems;

    std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> pboGroups;

    for (const auto* list : {&osFiles, &vfsFiles})
    {
        for (const auto& f : *list)
        {
            std::string ext = f.extension;
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (!((scanModels && ext == ".p3d") || (scanFonts && ext == ".fxy")))
                continue;

            ScanItem item;
            item.name = f.name;
            item.extension = ext;

            if (f.isVirtual())
            {
                int idx = (int)scanItems.size();
                pboGroups[f.pboPath].emplace_back(idx, f.pboFilename);
                item.inspectPath = "";
                item.needsCleanup = true;
            }
            else
            {
                item.inspectPath = f.fullPath;
                item.needsCleanup = false;
            }
            scanItems.push_back(std::move(item));
        }
    }

    for (auto& [pboPath, entries] : pboGroups)
    {
        try
        {
            QFBank bank;
            std::string bankName = StripPboExtension(pboPath);
            if (!bank.open(RString(bankName.c_str())))
                continue;
            bank.Lock();
            if (bank.error())
                continue;

            for (auto& [idx, pboFilename] : entries)
            {
                try
                {
                    Ref<IFileBuffer> data = bank.Read(pboFilename.c_str());
                    if (!data || data->GetSize() == 0)
                        continue;
                    auto tempDir = std::filesystem::temp_directory_path();
                    auto ext = std::filesystem::path(pboFilename).extension().string();
                    int id = gExtractCounter++;
                    std::string tmpPath = (tempDir / ("poseidon_xref_" + std::to_string(id) + ext)).string();
                    std::ofstream out(tmpPath, std::ios::binary);
                    if (!out)
                        continue;
                    out.write(static_cast<const char*>(data->GetData()), data->GetSize());
                    out.close();
                    scanItems[idx].inspectPath = tmpPath;
                }
                catch (...)
                {
                }
            }
        }
        catch (...)
        {
        }
    }

    scanItems.erase(
        std::remove_if(scanItems.begin(), scanItems.end(), [](const ScanItem& s) { return s.inspectPath.empty(); }),
        scanItems.end());

    usedByScanTotal = (int)scanItems.size();

    // The scan thread uses only standard C++ and Tools helpers; engine objects stay on the main thread.
    usedByThread = std::thread(
        [this, texName, texBasename, scanItems = std::move(scanItems)]()
        {
            std::vector<AssetRef> results;

            for (int fi = 0; fi < (int)scanItems.size(); fi++)
            {
                if (usedByCancelFlag)
                    break;
                usedByScanProgress = fi + 1;

                try
                {
                    const auto& item = scanItems[fi];
                    bool found = false;

                    if (item.extension == ".p3d")
                    {
                        auto info = InspectModel(item.inspectPath);
                        if (info.valid)
                        {
                            for (const auto& lod : info.lods)
                            {
                                for (const auto& tex : lod.textureNames)
                                {
                                    std::string normTex = normalizePath(tex);
                                    if (normTex == texName || normTex == texBasename)
                                    {
                                        found = true;
                                        break;
                                    }
                                    if (normTex.size() > texName.size() &&
                                        normTex[normTex.size() - texName.size() - 1] == '/' &&
                                        normTex.compare(normTex.size() - texName.size(), texName.size(), texName) == 0)
                                    {
                                        found = true;
                                        break;
                                    }
                                }
                                if (found)
                                    break;
                            }
                        }
                    }
                    else if (item.extension == ".fxy")
                    {
                        auto info = InspectFont(item.inspectPath);
                        if (info.valid)
                        {
                            for (const auto& ft : info.textureNames)
                            {
                                std::string normFt = normalizePath(ft);
                                if (normFt == texName || normFt == texBasename)
                                {
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (item.needsCleanup)
                    {
                        std::error_code ec;
                        std::filesystem::remove(item.inspectPath, ec);
                    }

                    if (found)
                    {
                        AssetRef ref;
                        ref.name = item.name;
                        ref.vfsPath = item.name;
                        ref.exists = true;
                        results.push_back(ref);
                    }
                }
                catch (...)
                {
                }
            }

            if (!usedByCancelFlag)
            {
                std::lock_guard<std::mutex> lk(usedByMutex);
                assetUsedBy = std::move(results);
            }
            usedByScanDone = true;
            usedByScanning = false;
        });
}

void StudioApp::setupTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.15f, 0.16f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.33f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.30f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.38f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.35f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.38f, 0.42f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.30f, 0.35f, 1.0f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);

    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 4);
}

void StudioApp::init(const std::string& path, SDL_Renderer* renderer)
{
    sdlRenderer = renderer;
    changeDirectory(path);
}

void StudioApp::changeDirectory(const std::string& newPath)
{
    if (scanThread.joinable())
        scanThread.join();

    osFiles.clear();
    vfsFiles.clear();
    filteredFiles.clear();
    searchFilter.clear();
    activeCategory = FileCategory::All;
    selectedFile.clear();
    selectedEntry = {};
    selectedIndex = -1;
    currentPreview = PreviewType::None;
    previewInfo.clear();
    clearPreviewTexture();
    clearGridThumbs();
    selectedIndices.clear();
    lastClickedIndex = -1;
    gridDetailMode = false;
    gridDetailIndex = -1;
    gridDetailEntry = {};
    stopSound();
    cachedInfoFile.clear();
    cachedInfoText.clear();
    cachedPboFile.clear();
    cachedPboInfo = {};
    scanning = false;
    scanDone = false;

    gamePath = newPath;
    if (gamePath.empty())
        gamePath = ".";

    gamePathValid = std::filesystem::exists(gamePath) && std::filesystem::is_directory(gamePath);
    if (gamePathValid)
    {
        log("Scanning: " + gamePath);
        if (headless)
            scanFiles();
        else
        {
            scanning = true;
            scanThread = std::thread(&StudioApp::scanFilesThread, this);
        }
    }
    else
    {
        log("Warning: Path not found: " + gamePath);
    }
}

void StudioApp::selectFile(const std::string& filename)
{
    openFilePath = filename;
    activeTab = BrowserTab::FileSystem;
}

void StudioApp::refresh()
{
    if (scanning || gamePath.empty())
        return;

    std::string savedSelection = selectedIndices.empty() ? std::string() : selectedFile;
    BrowserTab savedTab = activeTab;
    std::string savedSearch = searchFilter;
    FileCategory savedCategory = activeCategory;

    if (scanThread.joinable())
        scanThread.join();

    osFiles.clear();
    vfsFiles.clear();
    filteredFiles.clear();
    searchFilter.clear();
    activeCategory = FileCategory::All;
    selectedFile.clear();
    selectedEntry = {};
    selectedIndex = -1;
    currentPreview = PreviewType::None;
    previewInfo.clear();
    clearPreviewTexture();
    clearGridThumbs();
    selectedIndices.clear();
    lastClickedIndex = -1;
    gridDetailMode = false;
    gridDetailIndex = -1;
    gridDetailEntry = {};
    stopSound();
    cachedInfoFile.clear();
    cachedInfoText.clear();
    cachedPboFile.clear();
    cachedPboInfo = {};
    assetUses.clear();
    {
        std::lock_guard<std::mutex> lk(usedByMutex);
        assetUsedBy.clear();
    }
    usedByScanDone = false;

    log("Refreshing: " + gamePath);

    if (headless)
    {
        scanFiles();
    }
    else
    {
        scanning = true;
        scanDone = false;
        scanThread = std::thread(&StudioApp::scanFilesThread, this);
    }

    activeTab = savedTab;

    searchFilter = savedSearch;
    activeCategory = savedCategory;

    if (headless)
    {
        applyFilter();
        if (!savedSelection.empty())
        {
            for (int i = 0; i < (int)filteredFiles.size(); i++)
            {
                if (filteredFiles[i].name == savedSelection)
                {
                    selectFileAt(i);
                    selectedIndices.clear();
                    selectedIndices.insert(i);
                    lastClickedIndex = i;
                    break;
                }
            }
        }
    }
    else
    {
        if (!savedSelection.empty())
            openFilePath = savedSelection;
    }
}

void StudioApp::scanFilesThread()
{
    std::vector<FileEntry> localOs;
    std::vector<FileEntry> localVfs;
    std::vector<FileEntry> pboFiles;

    {
        std::lock_guard<std::mutex> lk(scanStatusMutex);
        scanStatusText = "Scanning filesystem...";
    }

    try
    {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 gamePath, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            if (it->is_directory())
                continue;

            FileEntry fe;
            fe.fullPath = it->path().string();
            fe.name = std::filesystem::relative(it->path(), gamePath).string();
            fe.isDirectory = false;
            fe.extension = it->path().extension().string();

            std::error_code szEc;
            fe.size = static_cast<int64_t>(std::filesystem::file_size(it->path(), szEc));
            if (szEc)
                fe.size = 0;

            for (auto& c : fe.name)
                if (c == '\\')
                    c = '/';

            std::string extLower = fe.extension;
            std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
            fe.isPbo = (extLower == ".pbo");

            localOs.push_back(fe);

            if (fe.isPbo)
                pboFiles.push_back(fe);
        }
    }
    catch (...)
    {
    }

    {
        std::lock_guard<std::mutex> lk(scanStatusMutex);
        scanStatusText =
            "Scanning PBOs... (0/" + std::to_string(pboFiles.size()) + ") " + std::to_string(localOs.size()) + " files";
    }

    for (size_t pi = 0; pi < pboFiles.size(); ++pi)
    {
        const auto& pbo = pboFiles[pi];
        {
            std::lock_guard<std::mutex> lk(scanStatusMutex);
            scanStatusText =
                "Scanning PBOs... (" + std::to_string(pi + 1) + "/" + std::to_string(pboFiles.size()) + ") " + pbo.name;
        }
        try
        {
            auto info = InspectPbo(pbo.fullPath);
            if (!info.valid)
                continue;

            std::string prefix = pbo.name;
            for (const auto& pboEntry : info.entries)
            {
                FileEntry ve;
                ve.name = prefix + "/" + pboEntry.name;
                ve.fullPath = pbo.fullPath;
                ve.pboPath = pbo.fullPath;
                ve.pboFilename = pboEntry.name;
                ve.size = pboEntry.compressed ? pboEntry.uncompressedSize : pboEntry.length;

                for (auto& c : ve.name)
                    if (c == '\\')
                        c = '/';

                auto dotPos = pboEntry.name.rfind('.');
                if (dotPos != std::string::npos)
                    ve.extension = pboEntry.name.substr(dotPos);

                localVfs.push_back(std::move(ve));
            }
        }
        catch (...)
        {
        }
    }

    // Register PBOs into the game's VFS so engine path lookups can resolve virtual assets.
    GFileBanks.Clear();
    for (const auto& pbo : pboFiles)
    {
        std::filesystem::path pboFsPath(pbo.fullPath);
        std::string pboDir = pboFsPath.parent_path().string() + "\\";
        std::string pboStem = pboFsPath.stem().string();
        std::string pboStemLower = pboStem;
        std::transform(pboStemLower.begin(), pboStemLower.end(), pboStemLower.begin(), ::tolower);
        GFileBanks.Load(RString(pboDir.c_str()), RString(""), RString(pboStemLower.c_str()), true);
    }
    GUseFileBanks = !pboFiles.empty();

    std::sort(localOs.begin(), localOs.end(), [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });
    std::sort(localVfs.begin(), localVfs.end(), [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });

    {
        std::set<std::string> seen;
        auto it = std::remove_if(localOs.begin(), localOs.end(),
                                 [&seen](const FileEntry& f)
                                 {
                                     std::string lower = f.name;
                                     std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                                     return !seen.insert(lower).second;
                                 });
        localOs.erase(it, localOs.end());
    }

    osFiles = std::move(localOs);
    vfsFiles = std::move(localVfs);
    scanDone = true;
}

void StudioApp::handleScanComplete()
{
    if (scanThread.joinable())
        scanThread.join();
    scanning = false;

    log("OS files: " + std::to_string(osFiles.size()) + ", VFS files: " + std::to_string(vfsFiles.size()));

    if (vfsFiles.empty() && !osFiles.empty() && activeTab == BrowserTab::GameVFS)
    {
        activeTab = BrowserTab::FileSystem;
        tabSwitchPending = true;
    }

    applyFilter();

    if (!openFilePath.empty())
    {
        bool found = false;
        for (const auto* list : {&osFiles, &vfsFiles})
        {
            for (const auto& f : *list)
            {
                if (f.fullPath == openFilePath || f.name == openFilePath)
                {
                    selectedFile = f.name;
                    selectedEntry = f;

                    if (f.isVirtual())
                        activeTab = BrowserTab::GameVFS;
                    else
                        activeTab = BrowserTab::FileSystem;
                    applyFilter();

                    for (int i = 0; i < (int)filteredFiles.size(); i++)
                    {
                        if (filteredFiles[i].name == f.name)
                        {
                            selectedIndex = i;
                            selectedIndices.clear();
                            selectedIndices.insert(i);
                            lastClickedIndex = i;
                            break;
                        }
                    }

                    std::string ext = f.extension;
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    currentPreview = previewTypeFromExtension(ext);

                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        log("Opening: " + openFilePath + (found ? "" : " (not found)"));
        openFilePath.clear();
    }
}

void StudioApp::scanFiles()
{
    scanFilesThread();
    handleScanComplete();
}

void StudioApp::applyFilter()
{
    const auto& source = (activeTab == BrowserTab::GameVFS) ? vfsFiles : osFiles;

    for (int i = 0; i < FileCategoryCount; i++)
        categoryCounts[i] = 0;
    for (const auto& f : source)
    {
        auto cat = CategorizeExtension(f.extension);
        categoryCounts[static_cast<int>(cat)]++;
    }
    categoryCounts[static_cast<int>(FileCategory::All)] = static_cast<int>(source.size());

    std::string filterLower;
    if (!searchFilter.empty())
    {
        filterLower = searchFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
    }

    filteredFiles.clear();
    for (const auto& f : source)
    {
        if (activeCategory != FileCategory::All && CategorizeExtension(f.extension) != activeCategory)
            continue;
        if (!filterLower.empty())
        {
            std::string nameLower = f.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find(filterLower) == std::string::npos)
                continue;
        }
        filteredFiles.push_back(f);
    }
}

void StudioApp::log(const std::string& msg)
{
    logMessages.push_back(msg);
}

void StudioApp::loadImagePreview(const std::string& path)
{
    std::string cacheKey = path + ":mip" + std::to_string(textureMipLevel);
    if (previewTextureFile == cacheKey)
        return;
    clearPreviewTexture();

    if (!sdlRenderer)
        return;

    try
    {
        auto preview = (textureMipLevel > 0) ? PreviewTextureMip(path, textureMipLevel) : PreviewTexture(path);
        if (preview.width > 0 && preview.height > 0 && !preview.data.empty())
        {
            previewTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                               preview.width, preview.height);
            if (previewTexture)
            {
                SDL_UpdateTexture(previewTexture, nullptr, preview.data.data(), preview.width * 4);
                previewTexW = preview.width;
                previewTexH = preview.height;
                previewTextureFile = cacheKey;
                log("Preview loaded: " + std::to_string(preview.width) + "x" + std::to_string(preview.height) +
                    (textureMipLevel > 0 ? " (mip " + std::to_string(textureMipLevel) + ")" : ""));
            }
        }
    }
    catch (const std::exception& e)
    {
        log("Preview error: " + std::string(e.what()));
    }
}

void StudioApp::clearGridThumbs()
{
    for (auto& [k, t] : gridThumbs)
    {
        if (t.tex)
            SDL_DestroyTexture(t.tex);
    }
    gridThumbs.clear();
    gridThumbMipTarget = 0;
}

static int BestMipForSize(int baseW, int baseH, int mipCount, int targetSize)
{
    int best = 0;
    int bestDiff = std::max(baseW, baseH);
    for (int m = 0; m < mipCount; m++)
    {
        int mw = std::max(1, baseW >> m);
        int mh = std::max(1, baseH >> m);
        int diff = std::abs(std::max(mw, mh) - targetSize);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = m;
        }
    }
    return best;
}

StudioApp::GridThumb& StudioApp::getOrLoadThumb(const FileEntry& entry)
{
    auto it = gridThumbs.find(entry.name);
    if (it != gridThumbs.end())
        return it->second;

    GridThumb& thumb = gridThumbs[entry.name];
    if (!sdlRenderer)
    {
        thumb.failed = true;
        return thumb;
    }

    try
    {
        std::string path = resolveFilePath(entry);
        if (path.empty())
        {
            thumb.failed = true;
            return thumb;
        }

        std::string ext = entry.extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        PreviewImage preview;
        if (ext == ".paa" || ext == ".pac")
        {
            PAAInfo paaInfo;
            if (ReadPAAInfo(path, paaInfo) && paaInfo.mipmapCount > 0)
            {
                int mip = BestMipForSize(paaInfo.width, paaInfo.height, paaInfo.mipmapCount, gridThumbSize);
                preview = PreviewTextureMip(path, mip);
            }
            if (!preview.valid())
                preview = PreviewTexture(path);
        }
        else
        {
            preview = PreviewTexture(path);
        }

        if (preview.valid())
        {
            thumb.tex = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                          preview.width, preview.height);
            if (thumb.tex)
            {
                SDL_UpdateTexture(thumb.tex, nullptr, preview.data.data(), preview.width * 4);
                thumb.w = preview.width;
                thumb.h = preview.height;
            }
            else
                thumb.failed = true;
        }
        else
            thumb.failed = true;
    }
    catch (...)
    {
        thumb.failed = true;
    }
    return thumb;
}

void StudioApp::evictOffscreenThumbs(int visibleStart, int visibleEnd, int cols)
{
    int keepStart = std::max(0, visibleStart - 2) * cols;
    int keepEnd = std::min((int)filteredFiles.size(), (visibleEnd + 2) * cols);

    std::unordered_set<std::string> keepSet;
    for (int i = keepStart; i < keepEnd && i < (int)filteredFiles.size(); i++)
        keepSet.insert(filteredFiles[i].name);

    for (auto it = gridThumbs.begin(); it != gridThumbs.end();)
    {
        if (keepSet.find(it->first) == keepSet.end())
        {
            if (it->second.tex)
                SDL_DestroyTexture(it->second.tex);
            it = gridThumbs.erase(it);
        }
        else
            ++it;
    }
}

void StudioApp::renderFileGrid(float w, float h)
{
    // Thumbnail size changes need different mip choices.
    if (gridThumbMipTarget != gridThumbSize)
    {
        clearGridThumbs();
        gridThumbMipTarget = gridThumbSize;
    }

    std::vector<int> gridItems;
    if (selectedIndices.empty())
    {
        gridItems.resize(filteredFiles.size());
        for (int i = 0; i < (int)filteredFiles.size(); i++)
            gridItems[i] = i;
    }
    else
    {
        gridItems.assign(selectedIndices.begin(), selectedIndices.end());
    }

    ImGui::BeginChild("FileGridOuter", ImVec2(w, h), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::Text("%d items", (int)gridItems.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderInt("##thumbsize", &gridThumbSize, 64, 256, "Size: %d");

    ImGui::Separator();

    float cellSize = (float)gridThumbSize + 8.0f;
    float textHeight = ImGui::GetTextLineHeightWithSpacing();
    float cellTotal = cellSize + textHeight + 4.0f;
    float gridH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("FileGrid", ImVec2(0, gridH), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    float availW = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)(availW / cellTotal));
    int totalItems = (int)gridItems.size();
    int rows = (totalItems + cols - 1) / cols;
    int loadBudget = 4;
    auto* dl = ImGui::GetWindowDrawList();
    const float thumbF = (float)gridThumbSize;
    const float pad = 4.0f;
    const ImU32 colBorder = IM_COL32(80, 80, 80, 255);
    const ImU32 colHover = IM_COL32(80, 80, 80, 100);

    ImGuiListClipper clipper;
    clipper.Begin(rows, cellTotal);

    int visStart = 0, visEnd = 0;
    while (clipper.Step())
    {
        visStart = clipper.DisplayStart;
        visEnd = clipper.DisplayEnd;

        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
        {
            for (int col = 0; col < cols; col++)
            {
                int itemIdx = row * cols + col;
                if (itemIdx >= totalItems)
                    break;

                int fileIdx = gridItems[itemIdx];
                if (fileIdx < 0 || fileIdx >= (int)filteredFiles.size())
                    continue;

                const auto& f = filteredFiles[fileIdx];
                if (col > 0)
                    ImGui::SameLine();

                ImGui::PushID(itemIdx);

                ImVec2 origin = ImGui::GetCursorScreenPos();
                ImVec2 cellEnd(origin.x + cellSize, origin.y + cellTotal);
                ImGui::InvisibleButton("##cell", ImVec2(cellSize, cellTotal));
                bool clicked = ImGui::IsItemClicked();
                bool hovered = ImGui::IsItemHovered();

                if (clicked)
                {
                    gridDetailMode = true;
                    gridDetailIndex = fileIdx;
                    gridDetailEntry = f;
                    selectFileAt(fileIdx);
                }

                if (hovered)
                    dl->AddRectFilled(origin, cellEnd, colHover, 2.0f);

                dl->AddRect(origin, cellEnd, colBorder, 2.0f, 1.0f, 0);
                ImVec2 thumbOrigin(origin.x + pad, origin.y + pad);
                std::string ext = f.extension;
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                bool isImage = (ext == ".paa" || ext == ".pac" || ext == ".png" || ext == ".bmp" || ext == ".tga" ||
                                ext == ".jpg");

                bool drewThumb = false;
                if (isImage)
                {
                    bool hasThumb = gridThumbs.count(f.name) > 0;
                    GridThumb* pThumb = nullptr;
                    if (hasThumb || loadBudget > 0)
                    {
                        if (!hasThumb)
                            loadBudget--;
                        pThumb = &getOrLoadThumb(f);
                    }
                    if (pThumb && pThumb->tex && pThumb->w > 0 && pThumb->h > 0)
                    {
                        float scale = std::min(thumbF / pThumb->w, thumbF / pThumb->h);
                        ImVec2 imgSz(pThumb->w * scale, pThumb->h * scale);
                        float padX = (thumbF - imgSz.x) * 0.5f;
                        float padY = (thumbF - imgSz.y) * 0.5f;
                        ImVec2 imgMin(thumbOrigin.x + padX, thumbOrigin.y + padY);
                        ImVec2 imgMax(imgMin.x + imgSz.x, imgMin.y + imgSz.y);
                        dl->AddImage(ImTextureRef((ImTextureID)pThumb->tex), imgMin, imgMax);
                        drewThumb = true;
                    }
                }

                if (!drewThumb)
                {
                    std::string lbl = ext.empty() ? "?" : ext.substr(1);
                    std::transform(lbl.begin(), lbl.end(), lbl.begin(), ::toupper);
                    ImVec2 txtSz = ImGui::CalcTextSize(lbl.c_str());
                    dl->AddText(
                        ImVec2(thumbOrigin.x + (thumbF - txtSz.x) * 0.5f, thumbOrigin.y + (thumbF - txtSz.y) * 0.5f),
                        IM_COL32(140, 160, 180, 255), lbl.c_str());
                }
                std::string label = f.name;
                auto slash = label.rfind('/');
                if (slash != std::string::npos)
                    label = label.substr(slash + 1);
                float textY = origin.y + cellSize;
                float maxTextW = cellSize - pad * 2;
                ImVec2 txtSz = ImGui::CalcTextSize(label.c_str());
                if (txtSz.x > maxTextW)
                {
                    std::string trunc;
                    for (size_t i = 0; i < label.size(); i++)
                    {
                        trunc += label[i];
                        if (ImGui::CalcTextSize((trunc + "...").c_str()).x > maxTextW)
                        {
                            trunc.pop_back();
                            trunc += "...";
                            break;
                        }
                    }
                    label = trunc;
                }
                ImU32 txtCol = IM_COL32(220, 220, 220, 255);
                dl->AddText(ImVec2(origin.x + pad, textY), txtCol, label.c_str());

                ImGui::PopID();
            }
        }
    }

    if (gridThumbs.size() > 100)
        evictOffscreenThumbs(visStart, visEnd, cols);

    ImGui::EndChild();
    ImGui::EndChild();
}

void StudioApp::loadTerrainPreview(const std::string& path)
{
    if (previewTextureFile == path)
        return;
    clearPreviewTexture();

    if (!sdlRenderer)
        return;

    try
    {
        auto preview = PreviewTerrain(path);
        if (preview.width > 0 && preview.height > 0 && !preview.data.empty())
        {
            previewTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                               preview.width, preview.height);
            if (previewTexture)
            {
                SDL_UpdateTexture(previewTexture, nullptr, preview.data.data(), preview.width * 4);
                previewTexW = preview.width;
                previewTexH = preview.height;
                previewTextureFile = path;
                log("Terrain preview loaded: " + std::to_string(preview.width) + "x" + std::to_string(preview.height));
            }
        }
    }
    catch (const std::exception& e)
    {
        log("Terrain preview error: " + std::string(e.what()));
    }
}

void StudioApp::loadModelPreview(const std::string& path)
{
    // Cache key includes path, LOD, and view
    std::string key = path + ":" + std::to_string(modelLodIndex) + ":" + modelView;
    if (modelPreviewKey == key)
        return;
    clearPreviewTexture();
    modelPreviewKey = key;

    if (!sdlRenderer)
        return;

    try
    {
        ModelPreviewOptions opts;
        opts.width = 512;
        opts.height = 512;
        opts.lodIndex = modelLodIndex;
        opts.view = modelView;
        auto preview = PreviewModel(path, opts);
        if (preview.valid())
        {
            previewTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
                                               preview.width, preview.height);
            if (previewTexture)
            {
                SDL_UpdateTexture(previewTexture, nullptr, preview.data.data(), preview.width * 3);
                previewTexW = preview.width;
                previewTexH = preview.height;
                previewTextureFile = path;
                log("Model preview: LOD " + std::to_string(modelLodIndex) + " [" + modelView + "]");
            }
        }
    }
    catch (const std::exception& e)
    {
        log("Model preview error: " + std::string(e.what()));
    }
}

void StudioApp::renderLoadingScreen(const ImVec2& displaySize)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::Begin("PoseidonStudio", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    float cx = displaySize.x * 0.5f;
    float cy = displaySize.y * 0.5f;

    ImGui::SetCursorPos(ImVec2(cx - 100.0f, cy - 30.0f));
    ImGui::Text("PoseidonStudio");
    float time = (float)ImGui::GetTime();
    const char* spinner = "|/-\\";
    char spin = spinner[(int)(time * 4.0f) % 4];

    std::string status;
    {
        std::lock_guard<std::mutex> lk(scanStatusMutex);
        status = scanStatusText;
    }

    ImGui::SetCursorPos(ImVec2(cx - 150.0f, cy + 5.0f));
    ImGui::Text("%c  %s", spin, status.c_str());

    ImGui::End();
}

void StudioApp::render(const ImVec2& displaySize)
{
    if (scanning && scanDone)
    {
        handleScanComplete();
    }
    if (scanning && !singleFileMode)
    {
        renderLoadingScreen(displaySize);
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::Begin("PoseidonStudio", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float statusBarH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 2.0f;
    float availH = ImGui::GetContentRegionAvail().y - statusBarH;
    float totalW = ImGui::GetContentRegionAvail().x;
    float logH = logCollapsed ? 30.0f : 120.0f;
    float topH = availH - logH;

    if (singleFileMode)
    {
        renderPreviewPanel(0, 0, totalW, topH);
        renderLogPanel(0, 0, totalW, logH);
        ImGui::Separator();
        ImGui::TextDisabled("%s", selectedEntry.fullPath.c_str());
    }
    else
    {
        const float splitterW = 6.0f;
        float leftW = totalW * sidebarRatio;
        float rightW = totalW - leftW - splitterW;

        renderFileBrowser(0, 0, leftW, availH);
        ImGui::SameLine(0, 0);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::Button("##splitter", ImVec2(splitterW, availH));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (ImGui::IsItemActive())
        {
            float delta = ImGui::GetIO().MouseDelta.x;
            if (delta != 0.0f)
            {
                leftW += delta;
                leftW = std::clamp(leftW, totalW * 0.10f, totalW * 0.80f);
                sidebarRatio = leftW / totalW;
                rightW = totalW - leftW - splitterW;
            }
        }

        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        if (shouldShowGrid())
        {
            renderFileGrid(rightW, topH);
        }
        else
        {
            renderInfoPanel(0, 0, rightW, topH * 0.30f);
            renderPreviewPanel(0, 0, rightW, topH * 0.70f);
        }
        renderLogPanel(0, 0, rightW, logH);
        ImGui::EndGroup();

        ImGui::Separator();
        const char* tabName = (activeTab == BrowserTab::GameVFS) ? "VFS" : "FS";
        ImGui::Text("%s: %d/%d files | Path: %s", tabName, (int)filteredFiles.size(),
                    (int)(activeTab == BrowserTab::GameVFS ? vfsFiles.size() : osFiles.size()), gamePath.c_str());
    }

    ImGui::End();
}

void StudioApp::renderFileBrowser(float, float, float w, float h)
{
    ImGui::BeginChild("FileBrowser", ImVec2(w, h), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("BrowserTabs"))
    {
        bool pending = tabSwitchPending || pendingTabSwitch >= 0;

        char vfsLabel[64];
        snprintf(vfsLabel, sizeof(vfsLabel), "Game VFS (%d)", (int)vfsFiles.size());
        ImGuiTabItemFlags vfsFlags = 0;
        if (activeTab == BrowserTab::GameVFS && !scanDone)
            vfsFlags = ImGuiTabItemFlags_SetSelected;
        if (pending && activeTab != BrowserTab::GameVFS)
            vfsFlags = 0;
        if (pendingTabSwitch == (int)BrowserTab::GameVFS)
            vfsFlags = ImGuiTabItemFlags_SetSelected;
        if (ImGui::BeginTabItem(vfsLabel, nullptr, vfsFlags))
        {
            if (!pending && activeTab != BrowserTab::GameVFS)
            {
                activeTab = BrowserTab::GameVFS;
                applyFilter();
            }
            ImGui::EndTabItem();
        }

        char fsLabel[64];
        snprintf(fsLabel, sizeof(fsLabel), "File System (%d)", (int)osFiles.size());
        ImGuiTabItemFlags fsFlags = 0;
        if ((tabSwitchPending && activeTab == BrowserTab::FileSystem) ||
            pendingTabSwitch == (int)BrowserTab::FileSystem)
        {
            fsFlags = ImGuiTabItemFlags_SetSelected;
        }
        if (ImGui::BeginTabItem(fsLabel, nullptr, fsFlags))
        {
            if (!pending && activeTab != BrowserTab::FileSystem)
            {
                activeTab = BrowserTab::FileSystem;
                applyFilter();
            }
            ImGui::EndTabItem();
        }

        tabSwitchPending = false;
        pendingTabSwitch = -1;
        float refreshBtnW = ImGui::CalcTextSize("↻").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(w - refreshBtnW - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::SmallButton("\xe2\x86\xbb##refresh"))
            refresh();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Refresh file list");

        ImGui::EndTabBar();
    }
    {
        char label[64];
        int cnt = categoryCounts[static_cast<int>(activeCategory)];
        snprintf(label, sizeof(label), "%s (%d)", FileCategoryName(activeCategory), cnt);
        float btnW = 50.0f;
        ImGui::SetNextItemWidth(w - 16.0f - btnW);
        if (ImGui::BeginCombo("##category", label))
        {
            for (int i = 0; i < FileCategoryCount; i++)
            {
                auto cat = static_cast<FileCategory>(i);
                int c = categoryCounts[i];
                if (c == 0 && cat != FileCategory::All)
                    continue;
                char item[64];
                snprintf(item, sizeof(item), "%s (%d)", FileCategoryName(cat), c);
                if (ImGui::Selectable(item, activeCategory == cat))
                {
                    activeCategory = cat;
                    applyFilter();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (selectedIndices.empty())
        {
            ImGui::BeginDisabled();
            ImGui::Button("Clear");
            ImGui::EndDisabled();
        }
        else
        {
            if (ImGui::Button("Clear"))
            {
                selectedIndices.clear();
                selectedIndex = -1;
                selectedFile.clear();
                selectedEntry = {};
                lastClickedIndex = -1;
                gridDetailMode = false;
                currentPreview = PreviewType::None;
                previewInfo.clear();
                clearPreviewTexture();
                stopSound();
                cachedInfoFile.clear();
                cachedInfoText.clear();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Clear selection (show all in grid)");
        }
    }
    static char searchBuf[256] = {0};
    if (searchFilter.empty() && searchBuf[0] != '\0')
        searchBuf[0] = '\0';
    float clearBtnW = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2 + 4.0f;
    bool hasSearch = searchBuf[0] != '\0';
    ImGui::SetNextItemWidth(w - 16.0f - (hasSearch ? clearBtnW + 4.0f : 0.0f));
    if (ImGui::InputTextWithHint("##search", "Search...", searchBuf, sizeof(searchBuf)))
    {
        searchFilter = searchBuf;
        applyFilter();
    }
    if (hasSearch)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("X##clear"))
        {
            searchBuf[0] = '\0';
            searchFilter.clear();
            applyFilter();
        }
    }

    ImGui::Separator();
    if (monoFont)
        ImGui::PushFont(monoFont, monoFont->LegacySize);
    ImGui::BeginChild("FileList", ImVec2(0, 0));
    if (ImGui::IsWindowFocused() && !filteredFiles.empty())
    {
        auto arrowSelect = [&](int newIdx)
        {
            selectFileAt(newIdx);
            selectedIndices.clear();
            selectedIndices.insert(newIdx);
            lastClickedIndex = newIdx;
            gridDetailMode = false;
            scrollToSelected = true;
        };

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            arrowSelect((selectedIndex > 0) ? selectedIndex - 1 : 0);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            arrowSelect((selectedIndex < (int)filteredFiles.size() - 1) ? selectedIndex + 1 : selectedIndex);
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
            arrowSelect(0);
        if (ImGui::IsKeyPressed(ImGuiKey_End))
            arrowSelect((int)filteredFiles.size() - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
            arrowSelect(std::max(0, selectedIndex - 20));
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
            arrowSelect(std::min((int)filteredFiles.size() - 1, selectedIndex + 20));
    }

    ImGuiListClipper clipper;
    clipper.Begin((int)filteredFiles.size());
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            const auto& f = filteredFiles[i];
            bool selected = selectedIndices.count(i) > 0;

            if (ImGui::Selectable(f.name.c_str(), selected))
            {
                bool ctrl = ImGui::GetIO().KeyCtrl;
                bool shift = ImGui::GetIO().KeyShift;

                if (shift && lastClickedIndex >= 0)
                {
                    int lo = std::min(lastClickedIndex, i);
                    int hi = std::max(lastClickedIndex, i);
                    if (!ctrl)
                        selectedIndices.clear();
                    for (int j = lo; j <= hi; j++)
                        selectedIndices.insert(j);
                }
                else if (ctrl)
                {
                    if (selectedIndices.count(i))
                        selectedIndices.erase(i);
                    else
                        selectedIndices.insert(i);
                }
                else
                {
                    selectedIndices.clear();
                    selectedIndices.insert(i);
                }
                lastClickedIndex = i;
                gridDetailMode = false;
                if (selectedIndices.size() == 1)
                    selectFileAt(*selectedIndices.begin());
                else
                {
                    selectedIndex = i;
                    selectedFile = f.name;
                }
            }
        }
    }

    // Scroll selected item into view only after keyboard navigation
    if (scrollToSelected && selectedIndex >= 0 && selectedIndex < (int)filteredFiles.size())
    {
        scrollToSelected = false;
        float itemHeight = ImGui::GetTextLineHeightWithSpacing();
        float scrollY = ImGui::GetScrollY();
        float windowH = ImGui::GetWindowHeight();
        float itemY = selectedIndex * itemHeight;
        if (itemY < scrollY)
            ImGui::SetScrollY(itemY);
        else if (itemY + itemHeight > scrollY + windowH)
            ImGui::SetScrollY(itemY + itemHeight - windowH);
    }

    ImGui::EndChild();
    if (monoFont)
        ImGui::PopFont();
    ImGui::EndChild();
}

void StudioApp::updateCachedInfo(const FileEntry& entry)
{
    if (cachedInfoFile == entry.name)
        return;

    cachedInfoFile = entry.name;
    cachedInfoText.clear();

    std::string resolvedPath = resolveFilePath(entry);
    if (resolvedPath.empty() && entry.isVirtual())
    {
        cachedInfoText = "Failed to extract file from PBO";
        return;
    }

    std::string ext = entry.extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::ostringstream ss;

    if (entry.isVirtual())
        ss << "Source: " << entry.pboPath << "\nPath in PBO: " << entry.pboFilename << "\n";

    if (ext == ".paa" || ext == ".pac")
    {
        auto info = InspectTexture(resolvedPath);
        if (info.valid)
        {
            textureMipCount = info.mipmapCount;
            textureOrigW = info.width;
            textureOrigH = info.height;
            ss << "Type: " << info.typeName << "\n";
            ss << "Format: " << info.formatName << " (0x" << std::hex << info.magic << std::dec << ")\n";
            ss << "Size: " << info.width << "x" << info.height << "\n";
            ss << "Mipmaps: " << info.mipmapCount << "\n";
            if (info.paletteColors > 0)
                ss << "Palette: " << info.paletteColors << " colors\n";
            if (info.magic == 0xFF01)
                ss << "Transparency: " << (info.hasTransparentBlocks ? "yes" : "no") << "\n";
        }
        else
        {
            ss << "ERROR: Failed to inspect texture\n";
        }
    }
    else if (ext == ".p3d")
    {
        auto info = InspectModel(resolvedPath);
        if (info.valid)
        {
            modelLodCount = info.lodCount;
            modelLodNames.clear();
            for (const auto& lod : info.lods)
                modelLodNames.push_back(std::to_string(lod.index) + ": " + lod.name);
            ss << "Format: " << info.format << " v" << info.version << "\n";
            ss << "LODs: " << info.lodCount << "\n";
            for (const auto& lod : info.lods)
                ss << "  LOD " << lod.index << ": " << lod.name << " (" << lod.points << " pts, " << lod.faces
                   << " faces, " << lod.textures << " tex)\n";
            assetUses.clear();
            std::set<std::string> seen;
            for (const auto& lod : info.lods)
            {
                for (const auto& tex : lod.textureNames)
                {
                    if (tex.empty())
                        continue;
                    std::string lower = tex;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if (!seen.insert(lower).second)
                        continue;
                    AssetRef ref;
                    ref.name = tex;
                    std::string cleanTex = tex;
                    if (!cleanTex.empty() && (cleanTex[0] == '\\' || cleanTex[0] == '/'))
                        cleanTex = cleanTex.substr(1);
                    if (entry.isVirtual())
                    {
                        std::string normTex = cleanTex;
                        std::replace(normTex.begin(), normTex.end(), '/', '\\');
                        ref.exists = QIFStreamB::FileExist(normTex.c_str());
                        std::string resolvedEngine = normTex;
                        if (!ref.exists)
                        {
                            std::string pboStem = std::filesystem::path(entry.pboPath).stem().string();
                            std::transform(pboStem.begin(), pboStem.end(), pboStem.begin(), ::tolower);
                            resolvedEngine = pboStem + "\\" + normTex;
                            ref.exists = QIFStreamB::FileExist(resolvedEngine.c_str());
                        }
                        if (ref.exists)
                            ref.vfsPath = resolveVfsDisplayPath(resolvedEngine);
                    }
                    else
                    {
                        std::filesystem::path modelDir = std::filesystem::path(resolvedPath).parent_path();
                        ref.exists = std::filesystem::exists(modelDir / cleanTex) ||
                                     std::filesystem::exists(std::filesystem::path(gamePath) / cleanTex);
                    }
                    assetUses.push_back(ref);
                }
            }
        }
    }
    else if (ext == ".wav" || ext == ".wss" || ext == ".ogg")
    {
        auto info = InspectSound(resolvedPath);
        if (info.valid)
        {
            ss << "Format: " << info.format << "\n";
            ss << "Channels: " << info.channels << "\n";
            ss << "Sample Rate: " << info.sampleRate << " Hz\n";
            ss << "Duration: " << std::fixed << std::setprecision(2) << info.duration << "s\n";
            ss << "Size: " << FormatSize(info.uncompressedSize) << "\n";
        }
    }
    else if (ext == ".wrp")
    {
        auto info = InspectTerrain(resolvedPath);
        if (info.valid)
        {
            ss << "Format: " << info.formatName << "\n";
            ss << "Grid: " << info.gridX << "x" << info.gridZ << "\n";
            ss << "Size: " << std::fixed << std::setprecision(0) << info.terrainX << "x" << info.terrainZ << " m\n";
            ss << "Elevation: " << std::setprecision(1) << info.minHeight << " - " << info.maxHeight << " m\n";
            ss << "Textures: " << info.textureCount << ", Objects: " << info.objectCount << "\n";
        }
    }
    else if (ext == ".pbo")
    {
        cachedPboFile = entry.fullPath;
        cachedPboInfo = InspectPbo(entry.fullPath);
        if (cachedPboInfo.valid)
        {
            ss << "Files: " << cachedPboInfo.entries.size() << "\n";
            ss << "Total: " << FormatSize(cachedPboInfo.totalSize) << "\n";
            ss << "Stored: " << FormatSize(cachedPboInfo.totalStored) << "\n";
        }
    }
    else if (ext == ".fxy")
    {
        assetUses.clear();
        if (entry.isVirtual())
        {
            QFBank bank;
            std::string bankName = StripPboExtension(entry.pboPath);
            if (bank.open(RString(bankName.c_str())))
            {
                bank.Lock();
                if (!bank.error())
                {
                    Ref<IFileBuffer> fxyBuf = bank.Read(entry.pboFilename.c_str());
                    if (fxyBuf && fxyBuf->GetSize() > 0)
                    {
                        std::string fontName = std::filesystem::path(entry.name).stem().string();
                        QIStream qs(static_cast<const char*>(fxyBuf->GetData()), static_cast<int>(fxyBuf->GetSize()));
                        FXYData fxy = ParseFXY(qs, fontName.c_str());
                        if (fxy.valid())
                        {
                            ss << "Name: " << fxy.name << "\n";
                            ss << "Glyphs: " << fxy.nChars << "\n";
                            ss << "Max Height: " << fxy.maxHeight << " px\n";
                            ss << "Max Width: " << fxy.maxWidth << " px\n";
                            ss << "Texture Sets: " << fxy.textureSetNums.size() << "\n";
                            for (int setNum : fxy.textureSetNums)
                            {
                                char texName[256];
                                snprintf(texName, sizeof(texName), "%s-%02d.paa", fontName.c_str(), setNum);
                                Ref<IFileBuffer> tb = bank.Read(texName);
                                AssetRef ref;
                                ref.name = texName;
                                ref.exists = (tb && tb->GetSize() > 0);
                                if (ref.exists)
                                {
                                    auto slashPos = entry.name.rfind('/');
                                    if (slashPos != std::string::npos)
                                        ref.vfsPath = entry.name.substr(0, slashPos + 1) + texName;
                                }
                                assetUses.push_back(ref);
                            }
                        }
                    }
                }
            }
        }
        else
        {
            auto info = InspectFont(entry.fullPath);
            if (info.valid)
            {
                ss << "Name: " << info.name << "\n";
                ss << "Glyphs: " << info.glyphCount << "\n";
                ss << "Max Height: " << info.maxHeight << " px\n";
                ss << "Max Width: " << info.maxWidth << " px\n";
                ss << "Texture Sets: " << info.textureSetCount << "\n";
                if (info.charCodeMin >= 0)
                    ss << "Char Range: " << info.charCodeMin << "-" << info.charCodeMax << "\n";
                for (size_t i = 0; i < info.textureNames.size(); i++)
                {
                    AssetRef ref;
                    ref.name = info.textureNames[i];
                    ref.exists = (i < info.texturesExist.size()) && info.texturesExist[i];
                    assetUses.push_back(ref);
                }
            }
            else
            {
                ss << "ERROR: Failed to inspect font\n";
            }
        }
    }
    else
    {
        auto info = InspectFile(resolvedPath.empty() ? entry.fullPath : resolvedPath);
        if (info.valid)
        {
            ss << "Name: " << info.name << "\n";
            ss << "Size: " << FormatSize(static_cast<long>(info.size)) << "\n";
            ss << "Extension: " << info.extension << "\n";
        }
    }

    cachedInfoText = ss.str();
}

void StudioApp::renderInfoPanel(float, float, float w, float h)
{
    ImGui::BeginChild("InfoPanel", ImVec2(w, h), ImGuiChildFlags_Borders);
    if (gridDetailMode)
    {
        if (ImGui::Button("< Back to Grid"))
        {
            gridDetailMode = false;
            gridDetailIndex = -1;
            ImGui::EndChild();
            return;
        }
        ImGui::SameLine();
    }
    ImGui::Text("Info");
    ImGui::Separator();

    if (selectedFile.empty())
    {
        ImGui::TextDisabled("Select a file to inspect");
        ImGui::EndChild();
        return;
    }
    updateCachedInfo(selectedEntry);
    ImGui::BeginChild("InfoContent", ImVec2(0, 0));
    if (!cachedInfoText.empty())
        ImGui::TextUnformatted(cachedInfoText.c_str());
    else
        ImGui::TextDisabled("No info available");

    renderCrossRefs(w);
    ImGui::EndChild();

    ImGui::EndChild();
}

void StudioApp::renderCrossRefs(float w)
{
    (void)w;
    std::string ext = selectedEntry.extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    bool isTexture =
        (ext == ".paa" || ext == ".pac" || ext == ".png" || ext == ".bmp" || ext == ".tga" || ext == ".jpg");
    bool isModel = (ext == ".p3d");
    bool isFont = (ext == ".fxy");
    if ((isModel || isFont) && !assetUses.empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Textures:");
        int foundCount = 0;
        for (const auto& ref : assetUses)
            if (ref.exists)
                foundCount++;

        for (size_t i = 0; i < assetUses.size(); i++)
        {
            const auto& ref = assetUses[i];
            ImGui::PushID(static_cast<int>(i));
            if (ref.exists)
            {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "  %s", ref.name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("->##use"))
                {
                    searchFilter.clear();
                    activeCategory = FileCategory::All;
                    applyFilter();
                    int idx = !ref.vfsPath.empty() ? findRefInFiltered(ref.vfsPath) : -1;
                    if (idx < 0)
                        idx = findRefInFiltered(ref.name);
                    if (idx >= 0)
                    {
                        selectedIndices.clear();
                        selectedIndices.insert(idx);
                        selectFileAt(idx);
                        lastClickedIndex = idx;
                        gridDetailMode = false;
                        scrollToSelected = true;
                    }
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "  %s (missing)", ref.name.c_str());
            }
            ImGui::PopID();
        }

        if (foundCount > 0)
        {
            if (ImGui::Button("Select All Found##uses"))
                selectFoundRefs(assetUses);
        }
    }
    if (isTexture)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Used By:");

        if (usedByScanning)
        {
            float progress = usedByScanTotal > 0 ? (float)usedByScanProgress / usedByScanTotal : 0.0f;
            ImGui::ProgressBar(
                progress, ImVec2(-1, 0),
                (std::to_string((int)usedByScanProgress) + "/" + std::to_string(usedByScanTotal)).c_str());
        }
        else if (usedByScanKey == selectedFile)
        {
            bool doNavigate = false;
            AssetRef navigateRef;
            bool selectAll = false;
            std::vector<AssetRef> usedByCopy;
            {
                std::lock_guard<std::mutex> lk(usedByMutex);
                if (assetUsedBy.empty())
                {
                    ImGui::TextDisabled("  No references found");
                }
                else
                {
                    for (size_t i = 0; i < assetUsedBy.size(); i++)
                    {
                        const auto& ref = assetUsedBy[i];
                        ImGui::PushID(static_cast<int>(1000 + i));
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "  %s", ref.name.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("->##usedby"))
                        {
                            navigateRef = ref;
                            doNavigate = true;
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Select All##usedby"))
                    {
                        selectAll = true;
                        usedByCopy = assetUsedBy;
                    }
                }
            }
            // Handle actions outside mutex scope
            if (doNavigate)
            {
                searchFilter.clear();
                activeCategory = FileCategory::All;
                applyFilter();
                int idx = !navigateRef.vfsPath.empty() ? findRefInFiltered(navigateRef.vfsPath) : -1;
                if (idx < 0)
                    idx = findRefInFiltered(navigateRef.name);
                if (idx >= 0)
                {
                    selectedIndices.clear();
                    selectedIndices.insert(idx);
                    selectFileAt(idx);
                    lastClickedIndex = idx;
                    gridDetailMode = false;
                    scrollToSelected = true;
                }
            }
            if (selectAll)
                selectFoundRefs(usedByCopy);
        }
        else
        {
            if (ImGui::Button("Find Used By"))
                startUsedByScan();
        }
    }
}

void StudioApp::renderPreviewPanel(float, float, float w, float h)
{
    ImGui::BeginChild("PreviewPanel", ImVec2(w, h), ImGuiChildFlags_Borders);
    ImGui::Text("Preview");
    if (currentPreview == PreviewType::Image && !selectedFile.empty())
    {
        ImGui::SameLine();
        ImGui::Text("Zoom: %.0f%%", textureZoom * 100.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("-##zoom"))
            textureZoom = std::max(0.1f, textureZoom * 0.8f);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##zoom"))
            textureZoom = std::min(8.0f, textureZoom * 1.25f);
        ImGui::SameLine();
        if (ImGui::SmallButton("1:1##zoom"))
            textureZoom = 1.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("Fit##zoom"))
        {
            if (previewTexW > 0 && previewTexH > 0)
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                textureZoom = std::min(avail.x / (float)previewTexW, avail.y / (float)previewTexH);
            }
        }
        if (textureMipCount > 1)
        {
            ImGui::SameLine();
            ImGui::Text("Mip:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);

            auto mipLabel = [&](int m) -> std::string
            {
                int mw = std::max(1, textureOrigW >> m);
                int mh = std::max(1, textureOrigH >> m);
                std::string s = std::to_string(mw) + "x" + std::to_string(mh);
                if (m == 0)
                    s += " (100%)";
                else
                {
                    float pct = 100.0f / (1 << m);
                    char buf[32];
                    snprintf(buf, sizeof(buf), " (%.4g%%)", pct);
                    s += buf;
                }
                return s;
            };

            if (ImGui::BeginCombo("##mip", mipLabel(textureMipLevel).c_str()))
            {
                for (int m = 0; m < textureMipCount; m++)
                {
                    bool selected = (textureMipLevel == m);
                    if (ImGui::Selectable(mipLabel(m).c_str(), selected))
                    {
                        textureMipLevel = m;
                        clearPreviewTexture();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
    }
    if (currentPreview == PreviewType::Config && !selectedFile.empty())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("+##cfgexpand"))
            configForceOpen = 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("-##cfgcollapse"))
            configForceOpen = -1;
        ImGui::SameLine();
        float searchW = ImGui::GetContentRegionAvail().x - 60.0f;
        if (searchW > 80.0f)
            ImGui::SetNextItemWidth(searchW);
        if (ImGui::InputText("##cfgsearch", configSearch, sizeof(configSearch), ImGuiInputTextFlags_EnterReturnsTrue))
            runConfigSearch();
        ImGui::SameLine();
        if (ImGui::SmallButton("X##cfgclear"))
        {
            memset(configSearch, 0, sizeof(configSearch));
            configSearchActive = false;
            configSearchResults.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Go##cfggo"))
            runConfigSearch();
    }
    ImGui::Separator();

    if (selectedFile.empty() || currentPreview == PreviewType::None)
    {
        ImGui::TextDisabled("Select a recognized file to preview");
    }
    else
    {
        ImGui::BeginChild("PreviewContent", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        switch (currentPreview)
        {
            case PreviewType::Image:
            {
                std::string path = resolveFilePath(selectedEntry);
                if (!path.empty())
                    loadImagePreview(path);
                if (previewTexture)
                {
                    if (ImGui::IsWindowHovered())
                    {
                        float wheel = ImGui::GetIO().MouseWheel;
                        if (wheel != 0.0f)
                        {
                            float oldZoom = textureZoom;
                            textureZoom = std::clamp(textureZoom * (1.0f + wheel * 0.1f), 0.1f, 8.0f);
                            float ratio = textureZoom / oldZoom;
                            ImVec2 mouse = ImGui::GetMousePos();
                            ImVec2 winPos = ImGui::GetWindowPos();
                            float scrollX = ImGui::GetScrollX();
                            float scrollY = ImGui::GetScrollY();
                            float mx = mouse.x - winPos.x + scrollX;
                            float my = mouse.y - winPos.y + scrollY;
                            ImGui::SetScrollX(mx * ratio - (mouse.x - winPos.x));
                            ImGui::SetScrollY(my * ratio - (mouse.y - winPos.y));
                        }
                    }
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        texturePanning = true;
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        texturePanning = false;
                    if (texturePanning)
                    {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
                        ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
                        if (delta.x != 0.0f || delta.y != 0.0f)
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    }

                    float displayW = previewTexW * textureZoom;
                    float displayH = previewTexH * textureZoom;
                    ImGui::Image(ImTextureRef((ImTextureID)previewTexture), ImVec2(displayW, displayH));
                }
                else
                {
                    ImGui::TextDisabled("Failed to load image preview");
                }
                break;
            }

            case PreviewType::Terrain:
            {
                std::string path = resolveFilePath(selectedEntry);
                if (!path.empty())
                    loadTerrainPreview(path);
                if (previewTexture)
                {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float scale = std::min(avail.x / (float)previewTexW, avail.y / (float)previewTexH);
                    if (scale > 1.0f)
                        scale = 1.0f;
                    ImGui::Image(ImTextureRef((ImTextureID)previewTexture),
                                 ImVec2(previewTexW * scale, previewTexH * scale));
                }
                else
                {
                    ImGui::TextDisabled("Failed to load terrain preview");
                }
                break;
            }

            case PreviewType::Model:
            {
                if (modelLodCount > 1 && !modelLodNames.empty())
                {
                    ImGui::Text("LOD:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(200.0f);
                    const char* currentLod =
                        (modelLodIndex < (int)modelLodNames.size()) ? modelLodNames[modelLodIndex].c_str() : "0";
                    if (ImGui::BeginCombo("##lod", currentLod))
                    {
                        for (int i = 0; i < (int)modelLodNames.size(); i++)
                        {
                            bool selected = (modelLodIndex == i);
                            if (ImGui::Selectable(modelLodNames[i].c_str(), selected))
                            {
                                modelLodIndex = i;
                                modelPreviewKey.clear();
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                }
                const char* views[] = {"front", "back", "left", "right", "top", "bottom", "quad", "3d"};
                ImGui::Text("View:");
                ImGui::SameLine();
                for (int v = 0; v < 8; v++)
                {
                    if (v > 0)
                        ImGui::SameLine();
                    bool active = (modelView == views[v]);
                    if (active)
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::SmallButton(views[v]))
                    {
                        modelView = views[v];
                        modelPreviewKey.clear();
                    }
                    if (active)
                        ImGui::PopStyleColor();
                }

                std::string path = resolveFilePath(selectedEntry);
                if (!path.empty())
                    loadModelPreview(path);
                if (previewTexture)
                {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float scale = std::min(avail.x / (float)previewTexW, avail.y / (float)previewTexH);
                    if (scale > 1.0f)
                        scale = 1.0f;
                    ImGui::Image(ImTextureRef((ImTextureID)previewTexture),
                                 ImVec2(previewTexW * scale, previewTexH * scale));
                }
                else
                {
                    ImGui::TextDisabled("Failed to load model preview");
                }
                break;
            }

            case PreviewType::Pbo:
            {
                if (cachedPboFile != selectedEntry.fullPath)
                {
                    cachedPboFile = selectedEntry.fullPath;
                    cachedPboInfo = InspectPbo(selectedEntry.fullPath);
                }

                if (cachedPboInfo.valid && !cachedPboInfo.entries.empty())
                {
                    ImGui::Text("PBO Contents (%d files):", (int)cachedPboInfo.entries.size());
                    ImGui::Separator();
                    if (ImGui::BeginTable("PboFiles", 3,
                                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                          ImGui::GetContentRegionAvail()))
                    {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("Stored", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableHeadersRow();

                        for (const auto& entry : cachedPboInfo.entries)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(entry.name.c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", FormatSize(entry.length).c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", FormatSize(static_cast<long>(entry.uncompressedSize)).c_str());
                        }
                        ImGui::EndTable();
                    }
                }
                else
                {
                    ImGui::TextDisabled("Failed to read PBO contents");
                }
                break;
            }

            case PreviewType::Sound:
            {
                if (soundPlaying && soundPlayer && !soundPlayer->isPlaying())
                {
                    soundPlaying = false;
                    log("Playback finished");
                }
                if (!soundPlaying)
                {
                    if (ImGui::Button("Play"))
                    {
                        playSoundEntry(selectedEntry);
                    }
                }
                else
                {
                    if (ImGui::Button("Stop"))
                        stopSound();

                    ImGui::SameLine();
                    float dur = soundPlayer ? soundPlayer->duration() : 0.0f;
                    if (dur > 0.0f)
                    {
                        auto elapsed =
                            std::chrono::duration<float>(std::chrono::steady_clock::now() - soundStartTime).count();
                        float progress = std::min(elapsed / dur, 1.0f);
                        char overlay[64];
                        snprintf(overlay, sizeof(overlay), "%.1f / %.1fs", elapsed, dur);
                        ImGui::ProgressBar(progress, ImVec2(-1, 0), overlay);
                    }
                }
                break;
            }

            case PreviewType::Font:
            {
                if (previewTextureFile != selectedEntry.name)
                {
                    clearPreviewTexture();
                    PreviewImage preview;
                    FontPreviewOptions fontOpts;
                    fontOpts.charmap = true;

                    if (selectedEntry.isVirtual())
                    {
                        // Load font entirely in memory from PBO (no temp files)
                        QFBank bank;
                        std::string bankName = StripPboExtension(selectedEntry.pboPath);
                        if (bank.open(RString(bankName.c_str())))
                        {
                            bank.Lock();
                            if (!bank.error())
                            {
                                Ref<IFileBuffer> fxyBuf = bank.Read(selectedEntry.pboFilename.c_str());
                                if (fxyBuf && fxyBuf->GetSize() > 0)
                                {
                                    std::string fontName = std::filesystem::path(selectedEntry.name).stem().string();
                                    QIStream qs(static_cast<const char*>(fxyBuf->GetData()),
                                                static_cast<int>(fxyBuf->GetSize()));
                                    FXYData fxy = ParseFXY(qs, fontName.c_str());
                                    std::vector<std::pair<int, std::pair<const void*, size_t>>> texSets;
                                    std::vector<Ref<IFileBuffer>> texBufs;
                                    for (int setNum : fxy.textureSetNums)
                                    {
                                        char texName[256];
                                        snprintf(texName, sizeof(texName), "%s-%02d.paa", fontName.c_str(), setNum);
                                        Ref<IFileBuffer> tb = bank.Read(texName);
                                        if (tb && tb->GetSize() > 0)
                                        {
                                            texSets.push_back({setNum, {tb->GetData(), tb->GetSize()}});
                                            texBufs.push_back(tb);
                                        }
                                    }

                                    preview = PreviewFontFromData(fxyBuf->GetData(), fxyBuf->GetSize(), fontName,
                                                                  texSets, fontOpts);
                                }
                            }
                        }
                    }
                    else
                    {
                        preview = PreviewFont(selectedEntry.fullPath, fontOpts);
                    }

                    if (preview.valid())
                    {
                        previewTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR8888,
                                                           SDL_TEXTUREACCESS_STATIC, preview.width, preview.height);
                        if (previewTexture)
                        {
                            SDL_UpdateTexture(previewTexture, nullptr, preview.data.data(), preview.width * 4);
                            previewTexW = preview.width;
                            previewTexH = preview.height;
                            previewTextureFile = selectedEntry.name;
                        }
                    }
                }
                if (previewTexture)
                {
                    float avail = ImGui::GetContentRegionAvail().x;
                    float scale = avail / static_cast<float>(previewTexW);
                    if (scale > 2.0f)
                        scale = 2.0f;
                    ImGui::Image(reinterpret_cast<ImTextureID>(previewTexture),
                                 ImVec2(previewTexW * scale, previewTexH * scale));
                }
                else
                {
                    ImGui::TextDisabled("Failed to load font preview");
                }
                break;
            }

            case PreviewType::Config:
                renderConfigPanel(0, 0, w, h);
                break;

            case PreviewType::Unknown:
                ImGui::TextDisabled("No preview available for this file type");
                break;
            default:
                break;
        }
        ImGui::EndChild();
    }

    ImGui::EndChild();
}

void StudioApp::renderLogPanel(float, float, float w, float h)
{
    ImGui::BeginChild("LogPanel", ImVec2(w, h), ImGuiChildFlags_Borders);
    bool wasCollapsed = logCollapsed;
    if (ImGui::SmallButton(logCollapsed ? ">" : "v"))
        logCollapsed = !logCollapsed;
    ImGui::SameLine();
    ImGui::Text("Log (%d)", (int)logMessages.size());

    if (!logCollapsed)
    {
        ImGui::Separator();
        ImGui::BeginChild("LogContent", ImVec2(0, 0));
        for (const auto& msg : logMessages)
            ImGui::TextUnformatted(msg.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }

    ImGui::EndChild();
}

static void RenderEntryValue(const ParamEntry& e)
{
    if (e.IsArray())
    {
        std::string val = e.GetName().Data();
        val += "[] = {";
        int show = std::min(e.GetSize(), 5);
        for (int j = 0; j < show; j++)
        {
            if (j > 0)
                val += ", ";
            const IParamArrayValue& av = e[j];
            if (av.IsFloatValue())
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", av.GetFloat());
                val += buf;
            }
            else if (av.IsIntValue())
                val += std::to_string(av.GetInt());
            else
            {
                val += '"';
                val += av.GetValue().Data();
                val += '"';
            }
        }
        if (e.GetSize() > 5)
            val += ", ...";
        val += "}";
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
        ImGui::TextUnformatted(val.c_str());
        ImGui::PopStyleColor();
    }
    else
    {
        std::string line = e.GetName().Data();
        line += " = ";
        if (e.IsFloatValue())
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", (float)e);
            line += buf;
        }
        else if (e.IsIntValue())
            line += std::to_string((int)e);
        else
        {
            line += '"';
            line += e.GetValue().Data();
            line += '"';
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }
}

void StudioApp::renderConfigClassTree(const ParamClass& cls, int forceOpen)
{
    for (int i = 0; i < cls.GetEntryCount(); i++)
    {
        const ParamEntry& e = cls.GetEntry(i);
        ImGui::PushID(i);
        if (e.IsClass())
        {
            const ParamClass* sub = e.GetClassInterface();
            std::string label = e.GetName().Data();
            const char* base = sub ? sub->GetBaseName() : nullptr;
            if (base)
            {
                label += " : ";
                label += base;
            }
            if (forceOpen != 0)
                ImGui::SetNextItemOpen(forceOpen > 0, ImGuiCond_Always);
            if (ImGui::TreeNode(label.c_str()))
            {
                if (sub)
                    renderConfigClassTree(*sub, forceOpen);
                ImGui::TreePop();
            }
        }
        else
        {
            RenderEntryValue(e);
        }
        ImGui::PopID();
    }
}

void StudioApp::collectConfigResults(const ParamClass& cls, const std::string& prefix, const std::string& query,
                                     std::vector<ConfigSearchResult>& out)
{
    for (int i = 0; i < cls.GetEntryCount(); i++)
    {
        const ParamEntry& e = cls.GetEntry(i);
        std::string name = e.GetName().Data();
        std::string path = prefix.empty() ? name : prefix + " >> " + name;

        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        std::string queryLower = query;
        std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

        if (nameLower.find(queryLower) != std::string::npos)
        {
            ConfigSearchResult r;
            r.path = path;
            r.entry = &e;
            out.push_back(r);
        }

        if (e.IsClass())
        {
            const ParamClass* sub = e.GetClassInterface();
            if (sub)
                collectConfigResults(*sub, path, query, out);
        }
    }
}

void StudioApp::runConfigSearch()
{
    configSearchResults.clear();
    std::string q = configSearch;
    while (!q.empty() && q.front() == ' ')
        q.erase(0, 1);
    while (!q.empty() && q.back() == ' ')
        q.pop_back();

    if (q.empty())
    {
        configSearchActive = false;
        return;
    }
    configSearchActive = true;

    if (!configFile)
        return;

    if (q.find(">>") != std::string::npos)
    {
        std::vector<std::string> parts;
        std::string rem = q;
        while (true)
        {
            auto pos = rem.find(">>");
            if (pos == std::string::npos)
            {
                while (!rem.empty() && rem.front() == ' ')
                    rem.erase(0, 1);
                while (!rem.empty() && rem.back() == ' ')
                    rem.pop_back();
                if (!rem.empty())
                    parts.push_back(rem);
                break;
            }
            std::string part = rem.substr(0, pos);
            while (!part.empty() && part.front() == ' ')
                part.erase(0, 1);
            while (!part.empty() && part.back() == ' ')
                part.pop_back();
            if (!part.empty())
                parts.push_back(part);
            rem = rem.substr(pos + 2);
        }

        const ParamClass* cur = configFile.get();
        std::string pathSoFar;
        for (size_t pi = 0; pi < parts.size() && cur; pi++)
        {
            const std::string& p = parts[pi];
            pathSoFar += (pathSoFar.empty() ? "" : " >> ") + p;
            const ParamEntry* found = cur->FindEntryNoInheritance(p.c_str());
            if (!found)
            {
                cur = nullptr;
                break;
            }
            if (pi == parts.size() - 1)
            {
                ConfigSearchResult r;
                r.path = pathSoFar;
                r.entry = found;
                configSearchResults.push_back(r);
            }
            else
            {
                cur = found->GetClassInterface();
            }
        }
    }
    else
    {
        collectConfigResults(*configFile, "", q, configSearchResults);
    }
}

void StudioApp::renderConfigPanel(float, float, float, float)
{
    std::string path = resolveFilePath(selectedEntry);
    if (path != configFilePath)
    {
        configFilePath = path;
        configSearchActive = false;
        memset(configSearch, 0, sizeof(configSearch));
        configSearchResults.clear();
        if (!path.empty())
        {
            auto cf = std::make_shared<ParamFile>();
            std::string ext = selectedEntry.extension;
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            bool ok = false;
            if (ext == ".bin")
                ok = cf->ParseBin(path.c_str());
            else if (ext == ".hpp")
                ok = cf->Parse(path.c_str()) == LSOK;
            else
                ok = cf->ParseBin(path.c_str()) || cf->Parse(path.c_str()) == LSOK;
            configFile = ok ? std::move(cf) : nullptr;
        }
        else
        {
            configFile = nullptr;
        }
    }

    if (!configFile)
    {
        ImGui::TextDisabled("Failed to parse config");
        return;
    }

    if (configSearchActive)
    {
        if (configSearchResults.empty())
        {
            ImGui::TextDisabled("No results for \"%s\"", configSearch);
        }
        else
        {
            ImGui::Text("%d result(s):", (int)configSearchResults.size());
            ImGui::Separator();
            for (const auto& r : configSearchResults)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 0.6f, 1.0f));
                ImGui::TextUnformatted(r.path.c_str());
                ImGui::PopStyleColor();
                if (r.entry && !r.entry->IsClass() && !r.entry->IsArray())
                {
                    ImGui::SameLine();
                    if (r.entry->IsFloatValue())
                    {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "= %g", (float)*r.entry);
                        ImGui::TextDisabled("%s", buf);
                    }
                    else if (r.entry->IsIntValue())
                        ImGui::TextDisabled("= %d", (int)*r.entry);
                    else
                        ImGui::TextDisabled("= \"%s\"", r.entry->GetValue().Data());
                }
            }
        }
    }
    else
    {
        renderConfigClassTree(*configFile, configForceOpen);
        configForceOpen = 0;
    }
}

} // namespace Poseidon
