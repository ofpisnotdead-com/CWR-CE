add_executable(GL33SmokeTest main.cpp)

target_link_libraries(GL33SmokeTest PRIVATE PoseidonGL33)

target_include_directories(GL33SmokeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/thirdparty/glad/include
)

set_target_properties(GL33SmokeTest PROPERTIES
    OUTPUT_NAME "GL33SmokeTest"
)
