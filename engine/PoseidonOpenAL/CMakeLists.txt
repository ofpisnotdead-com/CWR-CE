set(POSEIDON_OPENAL_SOURCES
    SoundSystemOAL.cpp
    WaveOAL.cpp
    Voice/VoNCaptureOpenAL.cpp
    Voice/MicLoopbackOpenAL.cpp
    Voice/VoiceBackendOpenAL.cpp
)

set(POSEIDON_OPENAL_HEADERS
    EFXPresets.hpp
    IosAudioSession.hpp
    OpenALRuntime.hpp
    SoundSystemOAL.hpp
    WaveOAL.hpp
    Voice/VoNCaptureOpenAL.hpp
    Voice/VoNSpeakerOpenAL.hpp
    Voice/MicLoopbackOpenAL.hpp
)

add_library(PoseidonOpenAL STATIC
    ${POSEIDON_OPENAL_SOURCES}
    ${POSEIDON_OPENAL_HEADERS}
)

if(POSEIDON_TARGET_IOS)
    target_sources(PoseidonOpenAL PRIVATE IosAudioSession.cpp)
endif()

find_package(OpenAL CONFIG REQUIRED)
set_target_properties(OpenAL::OpenAL PROPERTIES IMPORTED_GLOBAL TRUE)

target_link_libraries(PoseidonOpenAL PUBLIC Poseidon OpenAL::OpenAL)

if(WIN32)
    target_link_libraries(PoseidonOpenAL PUBLIC Avrt)
endif()

target_include_directories(PoseidonOpenAL PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
)
target_compile_definitions(PoseidonOpenAL PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

set_target_properties(PoseidonOpenAL PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "PoseidonOpenAL"
)
