// studio_ui_tests.cpp -- ImGui Test Engine integration tests for PoseidonStudio
// Headless SDL+ImGui context running StudioApp::render() in a loop.
// Synthetic fixture files: PAA texture, MLOD model, WAV sound, PBO archives.

#include "StudioApp.hpp"
#include "StudioConfig.hpp"

#include <Poseidon/Graphics/Textures/TextureBank.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <string.h>
#include <string>
#include <vector>
#include "FileCategory.hpp"

#pragma push_macro("DebugLog")
#pragma push_macro("Log")
#pragma push_macro("Sleep")
#undef DebugLog
#undef Log
#undef Sleep
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_te_engine.h>
#include <imgui_te_context.h>
#include <imgui_te_ui.h>
#pragma pop_macro("Sleep")
#pragma pop_macro("Log")
#pragma pop_macro("DebugLog")

#include <cstdio>
#include <filesystem>

using namespace Poseidon;

// PreviewType int values (mirrors StudioApp::PreviewType enum order)
enum
{
    PT_None = 0,
    PT_Image = 1,
    PT_Model = 2,
    PT_Sound = 3,
    PT_Pbo = 5,
    PT_Config = 7,
    PT_Unknown = 8
};
// BrowserTab int values
enum
{
    TAB_FileSystem = 0,
    TAB_GameVFS = 1
};

// Fixture file counts (tests/fixtures/studio).
// OS (recursive): Addons/{Vehicle,addon_fixture,font_fixture,mission_fixture.Intro}.pbo
//                 + ambient.wav, config.cpp, data/synthetic_grid.paa, readme.txt,
//                   script.sqs, vehicle.p3d
static constexpr int FIXTURE_OS_FILES = 10;
// VFS: addon_fixture.pbo(3) + font_fixture.pbo(2) + mission_fixture.Intro.pbo(3)
//      + Vehicle.pbo(2: vehicle.p3d, data/synthetic_grid.paa)
static constexpr int FIXTURE_VFS_FILES = 10;

// ---------------------------------------------------------------------------
// Globals & helpers
// ---------------------------------------------------------------------------
static StudioApp* g_app = nullptr;
static void RegisterTests(ImGuiTestEngine* engine);

static const char* findWindow(const char* partial)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Windows.Size; i++)
        if (strstr(g.Windows[i]->Name, partial))
            return g.Windows[i]->Name;
    return nullptr;
}

static ImGuiID widgetID(const char* windowName, const char* widgetName)
{
    ImGuiWindow* w = ImGui::FindWindowByName(windowName);
    return w ? w->GetID(widgetName) : 0;
}

static void resetToFS(ImGuiTestContext* ctx)
{
    g_app->testSetTab(TAB_FileSystem);
    g_app->testSetCategory(FileCategory::All);
    g_app->testResetSearch();
    g_app->testClearSelection();
    ctx->Yield(3);
}

static void resetToVFS(ImGuiTestContext* ctx)
{
    g_app->testSetTab(TAB_GameVFS);
    g_app->testSetCategory(FileCategory::All);
    g_app->testResetSearch();
    g_app->testClearSelection();
    ctx->Yield(3);
}

static void clickFile(ImGuiTestContext* ctx, const char* filename)
{
    const char* fl = findWindow("FileList");
    ImGuiID id = fl ? widgetID(fl, filename) : 0;
    if (id)
        ctx->ItemClick(id);
    ctx->Yield(3);
}

// ---------------------------------------------------------------------------
// Main harness
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::string fixturesDir;
    bool listOnly = false;
    std::string testFilter;

    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == "--list-tests")
            listOnly = true;
        else if (std::string(argv[i]) == "--filter" && i + 1 < argc)
            testFilter = argv[++i];
        else if (fixturesDir.empty())
            fixturesDir = argv[i];
    }

    if (listOnly)
    {
        // List test names without initializing SDL/ImGui render loop
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
        RegisterTests(engine);

        ImVector<ImGuiTest*> all_tests;
        ImGuiTestEngine_GetTestList(engine, &all_tests);
        for (int i = 0; i < all_tests.Size; i++)
            printf("%s/%s\n", all_tests[i]->Category, all_tests[i]->Name);

        ImGuiTestEngine_DestroyContext(engine);
        ImGui::DestroyContext();
        return 0;
    }

    if (fixturesDir.empty())
    {
        fixturesDir = "tests/Studio/fixtures";
        if (!std::filesystem::is_directory(fixturesDir))
        {
            fprintf(stderr, "Usage: %s [--list-tests] <fixtures_dir>\n", argv[0]);
            return 1;
        }
    }
    fixturesDir = std::filesystem::absolute(fixturesDir).string();

    Poseidon::NoTextures = true;

    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", false);
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("StudioTests", 1024, 768, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "software");
    if (!window || !renderer)
    {
        fprintf(stderr, "SDL setup failed\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024.0f, 768.0f);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(engine);
    test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    test_io.ConfigLogToTTY = true;
    test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());

    RegisterTests(engine);

    StudioApp app;
    app.headless = true;
    app.init(fixturesDir, renderer);
    g_app = &app;

    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, testFilter.empty() ? nullptr : testFilter.c_str());

    while (!ImGuiTestEngine_IsTestQueueEmpty(engine))
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            ImGui_ImplSDL3_ProcessEvent(&event);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app.render(io.DisplaySize);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 25, 25, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        ImGuiTestEngine_PostSwap(engine);
    }

    int tested = 0, succeeded = 0;
    ImGuiTestEngine_GetResult(engine, tested, succeeded);

    ImVector<ImGuiTest*> all_tests;
    ImGuiTestEngine_GetTestList(engine, &all_tests);
    for (int i = 0; i < all_tests.Size; i++)
    {
        ImGuiTest* t = all_tests[i];
        const char* status = (t->Output.Status == ImGuiTestStatus_Success) ? "PASS"
                             : (t->Output.Status == ImGuiTestStatus_Error) ? "FAIL"
                                                                           : "SKIP";
        printf("[%s] %s/%s\n", status, t->Category, t->Name);
    }
    printf("\n=== Studio UI Tests: %d/%d passed ===\n", succeeded, tested);

    ImGuiTestEngine_Stop(engine);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ImGuiTestEngine_DestroyContext(engine);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return (tested == succeeded) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Test definitions
// ---------------------------------------------------------------------------
static void RegisterTests(ImGuiTestEngine* engine)
{
    // ── Layout ──

    IM_REGISTER_TEST(engine, "layout", "panels_exist")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        IM_CHECK(findWindow("FileBrowser") != nullptr);
        IM_CHECK(findWindow("FileList") != nullptr);
        // Grid shown when no selection; preview shown when 1 selected
        bool hasGrid = findWindow("FileGridOuter") != nullptr;
        bool hasPreview = findWindow("PreviewPanel") != nullptr;
        IM_CHECK(hasGrid || hasPreview);
        IM_CHECK(findWindow("LogPanel") != nullptr);
    };

    IM_REGISTER_TEST(engine, "layout", "splitter")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->SetRef("PoseidonStudio");
        ctx->Yield(5);
        IM_CHECK(ctx->ItemExists("##splitter"));
        IM_CHECK(g_app->sidebarRatio > 0.05f);
        IM_CHECK(g_app->sidebarRatio < 0.95f);
    };

    // ── File Browser (OS tab) ──

    IM_REGISTER_TEST(engine, "browser", "os_file_count")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        IM_CHECK_EQ(g_app->testOsFileCount(), FIXTURE_OS_FILES);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_OS_FILES);
    };

    IM_REGISTER_TEST(engine, "browser", "vfs_file_count")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        IM_CHECK_EQ(g_app->testVfsFileCount(), FIXTURE_VFS_FILES);
    };

    IM_REGISTER_TEST(engine, "browser", "select_file")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        clickFile(ctx, "config.cpp");
        IM_CHECK_EQ(g_app->testSelectedFile(), std::string("config.cpp"));
        IM_CHECK(g_app->testSelectedIndex() >= 0);

        clickFile(ctx, "vehicle.p3d");
        IM_CHECK_EQ(g_app->testSelectedFile(), std::string("vehicle.p3d"));
    };

    IM_REGISTER_TEST(engine, "browser", "keyboard_nav")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        // Addons/Vehicle.pbo sorts first (uppercase 'V' < lowercase names) -> index 0
        clickFile(ctx, "Addons/Vehicle.pbo");
        IM_CHECK_EQ(g_app->testSelectedIndex(), 0);

        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectedIndex(), 1);

        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectedIndex(), 2);

        ctx->KeyPress(ImGuiKey_UpArrow);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectedIndex(), 1);

        ctx->KeyPress(ImGuiKey_Home);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectedIndex(), 0);

        ctx->KeyPress(ImGuiKey_End);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectedIndex(), g_app->testFilteredCount() - 1);
    };

    // ── Preview -- real file inspection ──

    IM_REGISTER_TEST(engine, "preview", "texture_paa")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        clickFile(ctx, "data/synthetic_grid.paa");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Image);
        // synthetic_grid.paa: DXT1, 64x64, 7 mipmaps
        IM_CHECK_EQ(g_app->testOrigW(), 64);
        IM_CHECK_EQ(g_app->testOrigH(), 64);
        IM_CHECK_EQ(g_app->testMipCount(), 7);
        IM_CHECK(g_app->testZoom() > 0.9f && g_app->testZoom() < 1.1f);
        // previewInfo should mention DXT1 and 64x64
        const auto& info = g_app->testPreviewInfo();
        IM_CHECK(info.find("DXT1") != std::string::npos);
        IM_CHECK(info.find("64x64") != std::string::npos);
        IM_CHECK(info.find("Mipmaps: 7") != std::string::npos);
    };

    IM_REGISTER_TEST(engine, "preview", "model_p3d")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        clickFile(ctx, "vehicle.p3d");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Model);
        // vehicle.p3d: MLOD, 1 LOD, 4 points, 1 face
        IM_CHECK_EQ(g_app->testModelLodCount(), 1);
        IM_CHECK(!g_app->testModelLodNames().empty());
        const auto& info = g_app->testPreviewInfo();
        IM_CHECK(info.find("MLOD") != std::string::npos);
        IM_CHECK(info.find("4 pts") != std::string::npos);   // 4 points
        IM_CHECK(info.find("1 faces") != std::string::npos); // 1 face
        // Texture cross-refs: data\synthetic_grid.paa is referenced and exists on disk
        IM_CHECK_EQ(g_app->testUsesCount(), 1);
        const auto& uses = g_app->testGetUses();
        IM_CHECK(uses[0].name.find("synthetic_grid.paa") != std::string::npos);
        IM_CHECK(uses[0].exists); // found next to the model on the filesystem
    };

    IM_REGISTER_TEST(engine, "preview", "sound_wav")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        clickFile(ctx, "ambient.wav");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Sound);
        // ambient.wav: 22050Hz, mono, 0.12s
        const auto& info = g_app->testPreviewInfo();
        IM_CHECK(info.find("WAV") != std::string::npos);
        IM_CHECK(info.find("22050") != std::string::npos);
        IM_CHECK(info.find("0.12") != std::string::npos); // duration
    };

    IM_REGISTER_TEST(engine, "preview", "pbo_archive")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        // PBO appears in OS file list as Addons/addon_fixture.pbo
        clickFile(ctx, "Addons/addon_fixture.pbo");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Pbo);
        const auto& info = g_app->testPreviewInfo();
        // addon_fixture.pbo: 3 files, total 229 B
        IM_CHECK(info.find("Files: 3") != std::string::npos);
        IM_CHECK(info.find("229") != std::string::npos);
    };

    IM_REGISTER_TEST(engine, "preview", "config_cpp")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        clickFile(ctx, "config.cpp");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Config);
        IM_CHECK_GE(g_app->testConfigTopLevelCount(), 2); // CfgVehicles + CfgWeapons
    };

    IM_REGISTER_TEST(engine, "config", "search_name")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "config.cpp");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Config);

        // Search for "speed" - should find Car.speed and Truck.speed
        g_app->testSetConfigSearch("speed");
        ctx->Yield(3);
        IM_CHECK(g_app->testConfigSearchActive());
        IM_CHECK_GE(g_app->testConfigSearchResultCount(), 2);
    };

    IM_REGISTER_TEST(engine, "config", "search_path")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "config.cpp");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Config);

        // Path navigation: CfgVehicles >> Car
        g_app->testSetConfigSearch("CfgVehicles >> Car");
        ctx->Yield(3);
        IM_CHECK(g_app->testConfigSearchActive());
        IM_CHECK_EQ(g_app->testConfigSearchResultCount(), 1);
    };

    IM_REGISTER_TEST(engine, "config", "search_clear")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "config.cpp");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Config);

        g_app->testSetConfigSearch("speed");
        ctx->Yield(3);
        IM_CHECK(g_app->testConfigSearchActive());
        IM_CHECK_GE(g_app->testConfigSearchResultCount(), 1);

        // Clear search
        g_app->testSetConfigSearch("");
        ctx->Yield(3);
        IM_CHECK(!g_app->testConfigSearchActive());
        IM_CHECK_EQ(g_app->testConfigSearchResultCount(), 0);
    };

    IM_REGISTER_TEST(engine, "config", "search_no_match")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "config.cpp");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Config);

        g_app->testSetConfigSearch("zzz_nonexistent");
        ctx->Yield(3);
        IM_CHECK(g_app->testConfigSearchActive());
        IM_CHECK_EQ(g_app->testConfigSearchResultCount(), 0);
    };

    IM_REGISTER_TEST(engine, "config", "search_path_leaf")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "config.cpp");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Config);

        // Deep path: CfgVehicles >> Car >> speed
        g_app->testSetConfigSearch("CfgVehicles >> Car >> speed");
        ctx->Yield(3);
        IM_CHECK(g_app->testConfigSearchActive());
        IM_CHECK_EQ(g_app->testConfigSearchResultCount(), 1);
    };

    IM_REGISTER_TEST(engine, "preview", "script_unknown")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        clickFile(ctx, "script.sqs");
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Unknown);
    };

    // ── Category Filter ──

    IM_REGISTER_TEST(engine, "filter", "category_combo_opens")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        ctx->ItemClick(widgetID(fb, "##category"));
        ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
    };

    IM_REGISTER_TEST(engine, "filter", "textures")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Textures);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "filter", "models")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Models);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "filter", "sounds")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Sounds);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "filter", "scripts")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Scripts);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "filter", "configs")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Configs);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "filter", "archives")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Archives);
        ctx->Yield(3);
        // Vehicle.pbo + addon_fixture.pbo + font_fixture.pbo + mission_fixture.Intro.pbo
        IM_CHECK_EQ(g_app->testFilteredCount(), 4);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "filter", "other")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Other);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1); // readme.txt
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "filter", "all_categories_sum")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        int total = 0;
        FileCategory cats[] = {FileCategory::Models,  FileCategory::Textures, FileCategory::Sounds,
                               FileCategory::Scripts, FileCategory::Configs,  FileCategory::Archives,
                               FileCategory::Other};
        for (auto cat : cats)
        {
            g_app->testSetCategory(cat);
            ctx->Yield(2);
            total += g_app->testFilteredCount();
        }
        g_app->testSetCategory(FileCategory::All);
        ctx->Yield(2);
        IM_CHECK_EQ(g_app->testFilteredCount(), total);
        IM_CHECK_EQ(total, FIXTURE_OS_FILES);
    };

    // ── Search ──

    IM_REGISTER_TEST(engine, "search", "filter_and_clear")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_OS_FILES);

        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);

        ctx->ItemInputValue(widgetID(fb, "##search"), "config");
        ctx->Yield(5);
        IM_CHECK(g_app->testFilteredCount() >= 1);
        IM_CHECK(g_app->testFilteredCount() < FIXTURE_OS_FILES);

        ctx->ItemClick(widgetID(fb, "X##clear"));
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_OS_FILES);
    };

    IM_REGISTER_TEST(engine, "search", "no_match")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);

        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);

        ctx->ItemInputValue(widgetID(fb, "##search"), "zzzznonexistent");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testFilteredCount(), 0);

        ctx->ItemClick(widgetID(fb, "X##clear"));
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_OS_FILES);
    };

    IM_REGISTER_TEST(engine, "search", "combined_with_category")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSetCategory(FileCategory::Textures);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);

        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);

        ctx->ItemInputValue(widgetID(fb, "##search"), "grid");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);

        ctx->ItemInputValue(widgetID(fb, "##search"), "vehicle");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testFilteredCount(), 0);

        ctx->ItemClick(widgetID(fb, "X##clear"));
        ctx->Yield(3);
        resetToFS(ctx);
    };

    // ── VFS Tab ──

    IM_REGISTER_TEST(engine, "vfs", "tab_switch")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        // Switch to VFS tab
        g_app->testSetTab(TAB_GameVFS);
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testActiveTab(), TAB_GameVFS);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_VFS_FILES);

        // Switch back to FS
        g_app->testSetTab(TAB_FileSystem);
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testActiveTab(), TAB_FileSystem);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_OS_FILES);
    };

    IM_REGISTER_TEST(engine, "vfs", "pbo_entries_listed")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        g_app->testSetTab(TAB_GameVFS);
        g_app->testSetCategory(FileCategory::All);
        g_app->testResetSearch();
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_VFS_FILES);
    };

    IM_REGISTER_TEST(engine, "vfs", "category_filter")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        g_app->testSetTab(TAB_GameVFS);
        ctx->Yield(3);

        // config.bin + description.ext -> Configs category
        g_app->testSetCategory(FileCategory::Configs);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), 2);

        // All again
        g_app->testSetCategory(FileCategory::All);
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_VFS_FILES);

        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "vfs", "search")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        g_app->testSetTab(TAB_GameVFS);
        g_app->testSetCategory(FileCategory::All);
        g_app->testResetSearch();
        ctx->Yield(5);

        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);

        // Search for "config" in VFS -> addon_fixture.pbo/config.bin
        ctx->ItemInputValue(widgetID(fb, "##search"), "config");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);

        ctx->ItemClick(widgetID(fb, "X##clear"));
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testFilteredCount(), FIXTURE_VFS_FILES);

        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "vfs", "model_texture_vfs_lookup")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        // Switch to VFS tab and select vehicle.p3d from Vehicle.pbo
        g_app->testSetTab(TAB_GameVFS);
        g_app->testSetCategory(FileCategory::All);
        g_app->testResetSearch();
        ctx->Yield(5);

        // Search for vehicle.p3d in VFS
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        ctx->ItemInputValue(widgetID(fb, "##search"), "vehicle.p3d");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);

        // Click VFS vehicle.p3d
        clickFile(ctx, "Addons/Vehicle.pbo/vehicle.p3d");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testPreviewType(), PT_Model);

        // Texture data\synthetic_grid.paa should be found via VFS (inside Vehicle.pbo)
        IM_CHECK_EQ(g_app->testUsesCount(), 1);
        const auto& uses = g_app->testGetUses();
        IM_CHECK(uses[0].name.find("synthetic_grid.paa") != std::string::npos);
        IM_CHECK(uses[0].exists); // found via VFS lookup

        resetToFS(ctx);
    };

    // ── Cross-References ──

    IM_REGISTER_TEST(engine, "xref", "model_uses_textures")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "vehicle.p3d");
        ctx->Yield(5);
        // Model should have 1 texture ref (data\synthetic_grid.paa)
        IM_CHECK_EQ(g_app->testUsesCount(), 1);
        IM_CHECK(g_app->testGetUses()[0].name.find("synthetic_grid.paa") != std::string::npos);
        // On the filesystem this texture exists next to the p3d (data/synthetic_grid.paa)
        IM_CHECK(g_app->testGetUses()[0].exists);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "xref", "vfs_model_uses_found")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToVFS(ctx);
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        ctx->ItemInputValue(widgetID(fb, "##search"), "vehicle.p3d");
        ctx->Yield(5);
        clickFile(ctx, "Addons/Vehicle.pbo/vehicle.p3d");
        ctx->Yield(5);
        // VFS model: texture should be found via VFS lookup
        IM_CHECK_EQ(g_app->testUsesCount(), 1);
        IM_CHECK(g_app->testGetUses()[0].exists);
        // vfsPath should be resolved for selection matching
        IM_CHECK(!g_app->testGetUses()[0].vfsPath.empty());
        IM_CHECK(g_app->testGetUses()[0].vfsPath.find("synthetic_grid.paa") != std::string::npos);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "xref", "texture_used_by_scan")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToVFS(ctx);
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        ctx->ItemInputValue(widgetID(fb, "##search"), "synthetic_grid.paa");
        ctx->Yield(5);
        clickFile(ctx, "Addons/Vehicle.pbo/data/synthetic_grid.paa");
        ctx->Yield(5);
        // Trigger "Used By" scan
        IM_CHECK(!g_app->testUsedByScanning());
        g_app->testTriggerUsedByScan();
        // Wait for scan to complete (small fixture set, should be fast)
        for (int i = 0; i < 300 && !g_app->testUsedByScanDone(); i++)
            ctx->Yield(1);
        IM_CHECK(g_app->testUsedByScanDone());
        IM_CHECK(!g_app->testUsedByScanning());
        // vehicle.p3d references data\synthetic_grid.paa
        IM_CHECK_GE(g_app->testUsedByCount(), 1);
        bool foundVehicle = false;
        for (const auto& r : g_app->testGetUsedBy())
            if (r.name.find("vehicle.p3d") != std::string::npos)
                foundVehicle = true;
        IM_CHECK(foundVehicle);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "xref", "used_by_scan_reselect")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToVFS(ctx);
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        ctx->ItemInputValue(widgetID(fb, "##search"), "synthetic_grid.paa");
        ctx->Yield(5);
        clickFile(ctx, "Addons/Vehicle.pbo/data/synthetic_grid.paa");
        ctx->Yield(5);
        // Start scan, then immediately switch selection (exercises mutex on assetUsedBy)
        g_app->testTriggerUsedByScan();
        ctx->Yield(1);
        ctx->ItemInputValue(widgetID(fb, "##search"), "vehicle.p3d");
        ctx->Yield(5);
        clickFile(ctx, "Addons/Vehicle.pbo/vehicle.p3d");
        ctx->Yield(5);
        // Wait for scan to finish (it may have been cancelled or completed)
        for (int i = 0; i < 300 && !g_app->testUsedByScanDone(); i++)
            ctx->Yield(1);
        // No crash is the main assertion; scan state should be consistent
        IM_CHECK(g_app->testUsedByScanDone() || !g_app->testUsedByScanning());
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "xref", "select_all_found_uses")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToVFS(ctx);
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        ctx->ItemInputValue(widgetID(fb, "##search"), "vehicle.p3d");
        ctx->Yield(5);
        clickFile(ctx, "Addons/Vehicle.pbo/vehicle.p3d");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testUsesCount(), 1);
        IM_CHECK(g_app->testGetUses()[0].exists);
        // Select all found uses -- should clear search/filter and select the texture
        g_app->testSelectFoundUses();
        ctx->Yield(5);
        IM_CHECK(g_app->testSearchFilter().empty());
        IM_CHECK_EQ(g_app->testCategory(), FileCategory::All);
        // The found texture should be selected
        IM_CHECK_GE(g_app->testSelectionCount(), 1);
        // With 1 ref selected, detail mode (not grid); grid needs 2+
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "xref", "select_found_clears_filter")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToVFS(ctx);
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        // Select model and note uses
        ctx->ItemInputValue(widgetID(fb, "##search"), "vehicle.p3d");
        ctx->Yield(5);
        clickFile(ctx, "Addons/Vehicle.pbo/vehicle.p3d");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testUsesCount(), 1);
        IM_CHECK(g_app->testGetUses()[0].exists);
        // Set a category filter too
        g_app->testSetCategory(FileCategory::Models);
        ctx->Yield(3);
        int filteredBefore = g_app->testFilteredCount();
        // Select found uses -- must clear both search and category
        g_app->testSelectFoundUses();
        ctx->Yield(5);
        IM_CHECK(g_app->testSearchFilter().empty());
        IM_CHECK_EQ(g_app->testCategory(), FileCategory::All);
        // filteredFiles should now contain all VFS files (more than filtered models)
        IM_CHECK_GT(g_app->testFilteredCount(), filteredBefore);
        IM_CHECK_GE(g_app->testSelectionCount(), 1);
        // Selected file should be the texture
        IM_CHECK(g_app->testSelectedFile().find("synthetic_grid.paa") != std::string::npos);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "xref", "navigate_clears_filter")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToVFS(ctx);
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        // Search for model, select it
        ctx->ItemInputValue(widgetID(fb, "##search"), "vehicle.p3d");
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testFilteredCount(), 1);
        g_app->testNavigateTo(0);
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testUsesCount(), 1);
        IM_CHECK(g_app->testGetUses()[0].exists);
        // With search "vehicle.p3d" active, the texture synthetic_grid.paa should NOT be
        // in filteredFiles -- so the old logic would hide the -> button
        IM_CHECK_EQ(g_app->testFindRef(g_app->testGetUses()[0].vfsPath), -1);
        // Simulate navigate: clear filters, then find the ref
        g_app->testResetSearch();
        g_app->testSetCategory(FileCategory::All);
        ctx->Yield(3);
        // Now the texture should be findable
        int idx = g_app->testFindRef(g_app->testGetUses()[0].vfsPath);
        IM_CHECK_GE(idx, 0);
        // Navigate to it
        g_app->testNavigateTo(idx);
        ctx->Yield(3);
        IM_CHECK(g_app->testSelectedFile().find("synthetic_grid.paa") != std::string::npos);
        IM_CHECK(g_app->testSearchFilter().empty());
        IM_CHECK_EQ(g_app->testCategory(), FileCategory::All);
        resetToFS(ctx);
    };

    // ── Refresh ──

    IM_REGISTER_TEST(engine, "browser", "refresh_preserves_selection")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "vehicle.p3d");
        ctx->Yield(5);
        IM_CHECK(g_app->testSelectedFile() == std::string("vehicle.p3d"));
        int countBefore = g_app->testOsFileCount();
        // Refresh
        g_app->testRefresh();
        ctx->Yield(5);
        // File counts should be the same
        IM_CHECK_EQ(g_app->testOsFileCount(), countBefore);
        // Selection should be preserved
        IM_CHECK(g_app->testSelectedFile() == std::string("vehicle.p3d"));
        IM_CHECK_EQ(g_app->testSelectionCount(), 1);
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "browser", "refresh_clears_invalid")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        // Select a file, then refresh -- file still exists, so selection preserved
        clickFile(ctx, "config.cpp");
        ctx->Yield(5);
        IM_CHECK(g_app->testSelectedFile() == std::string("config.cpp"));
        g_app->testRefresh();
        ctx->Yield(5);
        IM_CHECK(g_app->testSelectedFile() == std::string("config.cpp"));
        // Refresh with no selection should leave selection empty
        g_app->testResetSearch();
        g_app->testSelectMultiple({});
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectionCount(), 0);
        g_app->testRefresh();
        ctx->Yield(5);
        IM_CHECK(g_app->testSelectedFile().empty());
        IM_CHECK_EQ(g_app->testSelectionCount(), 0);
        IM_CHECK_GE(g_app->testOsFileCount(), 1);
        resetToFS(ctx);
    };

    // ── Multi-Select & Grid ──

    IM_REGISTER_TEST(engine, "multiselect", "no_selection_shows_grid")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        // No selection -> grid mode
        IM_CHECK(g_app->testGridMode());
        IM_CHECK_EQ(g_app->testSelectionCount(), 0);
        IM_CHECK(findWindow("FileGridOuter") != nullptr);
    };

    IM_REGISTER_TEST(engine, "multiselect", "single_click_shows_detail")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        clickFile(ctx, "config.cpp");
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectionCount(), 1);
        IM_CHECK(!g_app->testGridMode());
        IM_CHECK(findWindow("PreviewPanel") != nullptr);
    };

    IM_REGISTER_TEST(engine, "multiselect", "multi_select_shows_grid")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        // Programmatic multi-select
        g_app->testSelectMultiple({0, 1, 2});
        ctx->Yield(5);
        IM_CHECK_EQ(g_app->testSelectionCount(), 3);
        IM_CHECK(g_app->testGridMode());
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "multiselect", "clear_button")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        // Select a file first
        clickFile(ctx, "config.cpp");
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectionCount(), 1);

        // Click Clear button
        const char* fb = findWindow("FileBrowser");
        IM_CHECK(fb != nullptr);
        ctx->ItemClick(widgetID(fb, "Clear"));
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectionCount(), 0);
        IM_CHECK(g_app->testGridMode());
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "multiselect", "grid_detail_back")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSelectMultiple({0, 1});
        ctx->Yield(5);
        IM_CHECK(g_app->testGridMode());
        IM_CHECK_EQ(g_app->testSelectionCount(), 2);

        // Simulate grid click -> detail mode (invisible buttons don't have stable IDs)
        // This mirrors what renderFileGrid does on click
        g_app->testClearSelection();
        clickFile(ctx, "config.cpp");
        ctx->Yield(3);
        // Manually enter grid detail mode as if we came from grid
        g_app->testSelectMultiple({0, 1}); // restore multi-select
        // Simulate what grid click does: set gridDetailMode
        g_app->testSetGridDetail(2); // detail for index 2
        ctx->Yield(5);
        IM_CHECK(g_app->testGridDetailMode());
        IM_CHECK(!g_app->testGridMode());

        // Click back button
        const char* ip = findWindow("InfoPanel");
        IM_CHECK(ip != nullptr);
        ctx->ItemClick(widgetID(ip, "< Back to Grid"));
        ctx->Yield(3);
        IM_CHECK(!g_app->testGridDetailMode());
        IM_CHECK(g_app->testGridMode());
        IM_CHECK_EQ(g_app->testSelectionCount(), 2); // selection preserved
        resetToFS(ctx);
    };

    IM_REGISTER_TEST(engine, "multiselect", "keyboard_nav_single_selects")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        resetToFS(ctx);
        g_app->testSelectMultiple({0, 1, 2});
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectionCount(), 3);

        // Click a file to single-select (which also focuses the list)
        clickFile(ctx, "config.cpp");
        ctx->Yield(3);
        IM_CHECK_EQ(g_app->testSelectionCount(), 1);
        IM_CHECK(!g_app->testGridMode());
        resetToFS(ctx);
    };

    // ── Log Panel ──

    IM_REGISTER_TEST(engine, "log", "collapse_expand")->TestFunc = [](ImGuiTestContext* ctx)
    {
        ctx->Yield(10);
        const char* lp = findWindow("LogPanel");
        IM_CHECK(lp != nullptr);

        bool initial = g_app->testLogCollapsed();
        const char* btn = initial ? ">" : "v";
        ImGuiID tid = widgetID(lp, btn);
        if (tid)
        {
            ctx->ItemClick(tid);
            ctx->Yield(3);
            IM_CHECK_EQ(g_app->testLogCollapsed(), !initial);

            const char* btn2 = !initial ? ">" : "v";
            ImGuiID tid2 = widgetID(lp, btn2);
            if (tid2)
            {
                ctx->ItemClick(tid2);
                ctx->Yield(3);
                IM_CHECK_EQ(g_app->testLogCollapsed(), initial);
            }
        }
    };
}
