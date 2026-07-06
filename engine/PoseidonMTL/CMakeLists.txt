add_library(PoseidonMTL STATIC
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui_backends/imgui_impl_metal.h
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui_backends/imgui_impl_metal.mm
    DebugOverlayMetal.hpp
    DebugOverlayMetal.mm
    MetalCppImpl.cpp
    EngineMTLBootstrap.cpp
    EngineMTLBootstrap.hpp
    EngineMTL.cpp
    EngineMTL.hpp
    GraphicsBackendMTL.cpp
    TextureMTL.cpp
    TextureMTL.hpp
    TextBankMTL.cpp
    TextBankMTL.hpp
    VertexBufferMTL.cpp
    VertexBufferMTL.hpp
)

target_link_libraries(PoseidonMTL PUBLIC Poseidon)

target_include_directories(PoseidonMTL SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/thirdparty/metal-cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui_backends
)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(PoseidonMTL PUBLIC SDL3::SDL3)

find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
target_link_libraries(PoseidonMTL PUBLIC
    ${METAL_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
)

target_compile_definitions(PoseidonMTL PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    IMGUI_IMPL_METAL_CPP
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/thirdparty/imgui_backends/imgui_impl_metal.mm
    DebugOverlayMetal.mm
    PROPERTIES
        COMPILE_FLAGS "-fno-objc-arc"
)

set_target_properties(PoseidonMTL PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "PoseidonMTL"
)
