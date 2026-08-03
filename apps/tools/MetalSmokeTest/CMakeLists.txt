add_executable(MetalSmokeTest main.cpp)

target_link_libraries(MetalSmokeTest PRIVATE PoseidonMTL)

set_target_properties(MetalSmokeTest PROPERTIES
    OUTPUT_NAME "MetalSmokeTest"
)

if(POSEIDON_TARGET_IOS)
    # iOS executables are always app bundles -- give it a real bundle ID
    # and name so it's installable (CMake leaves these blank by default).
    set_target_properties(MetalSmokeTest PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.mcardle.cwr.metalsmoketest"
        MACOSX_BUNDLE_BUNDLE_NAME "MetalSmokeTest"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()
