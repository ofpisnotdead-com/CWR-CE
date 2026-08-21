#ifdef _MSC_VER
#pragma once
#endif

#ifndef POSEIDON_GRAPHICS_GRAPHICSENGINEFACTORY_HPP
#define POSEIDON_GRAPHICS_GRAPHICSENGINEFACTORY_HPP

namespace Poseidon
{
class Engine;

#ifdef _WIN32
} // namespace Poseidon
#include <Poseidon/Foundation/Common/Win.h>
namespace Poseidon
{
#else
} // namespace Poseidon
#include <Poseidon/Foundation/platform.hpp>
namespace Poseidon
{
#endif

} // namespace Poseidon
#include <string>
#include <vector>
namespace Poseidon
{

// Graphics backend selection.
enum class GraphicsBackend
{
    Dummy,     // Headless / server / harness — no rendering
    GL33 = 33, // OpenGL 3.3 Core Profile with SDL3 window
    Auto       // Automatically select the highest-priority available backend
};

// Engine creation parameters.
struct GraphicsEngineParams
{
    // Input parameters
    HINSTANCE hInst;
    HINSTANCE hPrev;
    int showCmd;
    int width;
    int height;
    int bitsPerPixel;

    // Input/Output parameters
    bool useWindow;
    std::string displayMode; // "windowed", "borderless", "exclusive"

    // Output parameters
    bool hideCursor;
    bool noRedrawWindow;

    GraphicsEngineParams()
        : hInst(0), hPrev(0), showCmd(0), width(640), height(480), bitsPerPixel(16), useWindow(false),
          displayMode("borderless"), hideCursor(false), noRedrawWindow(false)
    {
    }
};

using GraphicsBackendCreateFn = Engine* (*)(const GraphicsEngineParams& params);
using GraphicsBackendAvailableFn = bool (*)();

struct GraphicsBackendDescriptor
{
    const char* codeName;
    const char* displayName;
    int priority;
    GraphicsBackendCreateFn create;
    GraphicsBackendAvailableFn isAvailable;
};

struct GraphicsBackendInfo
{
    const char* codeName = nullptr;
    const char* displayName = nullptr;
    int priority = 0;
    bool isAvailable = false;
};

// Graphics engine factory
class GraphicsEngineFactory
{
  public:
    // Create graphics engine based on backend and configuration.
    // Caller takes ownership of the returned Engine*.  For Dummy,
    // params can be default-constructed.
    static Engine* Create(GraphicsBackend backend, const GraphicsEngineParams& params = GraphicsEngineParams());
    static Engine* Create(const std::string& requestedBackend,
                          const GraphicsEngineParams& params = GraphicsEngineParams());

    static bool Register(const GraphicsBackendDescriptor& descriptor);

    // Auto: highest-priority registered available backend.
    static Engine* CreateAuto(const GraphicsEngineParams& params);

    static std::vector<GraphicsBackendInfo> EnumerateRegistered();
    static std::vector<GraphicsBackendInfo> EnumerateAvailable();
    static bool IsBackendAvailable(GraphicsBackend backend);
    static const char* GetBackendName(GraphicsBackend backend);

    static void ResetForTesting();

  private:
    GraphicsEngineFactory() = delete;
};

void RegisterDummyGraphicsBackend();
void RegisterGL33GraphicsBackend();
void RegisterMetalGraphicsBackend(); // macOS / Apple Silicon only

} // namespace Poseidon
#endif // POSEIDON_GRAPHICS_GRAPHICSENGINEFACTORY_HPP
