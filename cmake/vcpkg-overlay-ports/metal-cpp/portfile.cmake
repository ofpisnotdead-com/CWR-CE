vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO apple/metal-cpp
    REF ${VERSION}
    SHA512 8c1fd6c13d8a1c3cc341454dbbec40b400b8fb2d83dd81d422f6396811828f14ebdd36f6152499671bbab1185422036c7b5043be328cdc2d203dc128a81947c9
    HEAD_REF main
)

file(INSTALL
    "${SOURCE_PATH}/Foundation"
    "${SOURCE_PATH}/Metal"
    "${SOURCE_PATH}/QuartzCore"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
