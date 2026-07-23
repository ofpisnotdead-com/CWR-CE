string(REGEX MATCH "^([0-9]+\\.[0-9]+)" VERSION_SHORT "${VERSION}")

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/util-linux/util-linux/archive/refs/tags/v${VERSION}.tar.gz"
    FILENAME "util-linux-${VERSION}.tar.gz"
    SHA512 747e988c6e35cb9254978aa8faeb2a0346d60b391cc44b1d150f9d6dc719cb2513294f9ddddef7c67a9b9dba00dabf52b69d34ffcebcab19c7a73ebb6bee8f66
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    SOURCE_BASE ${VERSION}
    PATCHES
        hide-private-symbols.diff
)

file(COPY "/usr/share/gettext/config.rpath" DESTINATION "${SOURCE_PATH}/config")
file(MAKE_DIRECTORY "${SOURCE_PATH}/po")
file(COPY "/usr/share/gettext/po/Makefile.in.in" DESTINATION "${SOURCE_PATH}/po")
vcpkg_replace_string("${SOURCE_PATH}/Makefile.am" "SUBDIRS = po" "SUBDIRS =")

set(ENV{GTKDOCIZE} true)

vcpkg_list(SET options)
if("nls" IN_LIST FEATURES)
    vcpkg_list(APPEND options "--enable-nls")
else()
    file(GLOB _gettext_m4
        "/usr/share/gettext/m4/gettext.m4"
        "/usr/share/gettext-*/m4/gettext.m4"
        "/usr/share/aclocal/gettext.m4")
    set(_aclocal_dirs "/usr/share/gettext/m4")
    foreach(_m4 IN LISTS _gettext_m4)
        get_filename_component(_dir "${_m4}" DIRECTORY)
        set(_aclocal_dirs "${_dir}:${_aclocal_dirs}")
    endforeach()
    set(ENV{ACLOCAL_PATH} "${_aclocal_dirs}")
    set(ENV{AUTOPOINT} true) # true, the program
    vcpkg_list(APPEND options "--disable-nls")
endif()
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm")
    vcpkg_list(APPEND options "--disable-year2038")
endif()

vcpkg_make_configure(
    AUTORECONF
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${options}
        --disable-asciidoc
        --disable-all-programs
        --disable-dependency-tracking
        --enable-libmount
        --enable-libblkid
        "--mandir=${CURRENT_PACKAGES_DIR}/share/man"
)

vcpkg_make_install()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/bin"
    "${CURRENT_PACKAGES_DIR}/debug/sbin"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/bin"
    "${CURRENT_PACKAGES_DIR}/sbin"
    "${CURRENT_PACKAGES_DIR}/share"
    "${CURRENT_PACKAGES_DIR}/tools"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/README.licensing" "${SOURCE_PATH}/COPYING")
