# FindOpenSlide.cmake
# Find OpenSlide library
#
# This module defines:
#  OPENSLIDE_FOUND - whether OpenSlide was found
#  OPENSLIDE_INCLUDE_DIRS - include directories for OpenSlide
#  OPENSLIDE_LIBRARIES - libraries to link against OpenSlide

# First try to find via vcpkg installed paths (relative to source dir)
set(_VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed")

# Determine vcpkg triplet
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_VCPKG_TRIPLET "x64-windows")
    else()
        set(_VCPKG_TRIPLET "x86-windows")
    endif()
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(_VCPKG_TRIPLET "x64-osx")
    else()
        set(_VCPKG_TRIPLET "arm64-osx")
    endif()
elseif(UNIX)
    set(_VCPKG_TRIPLET "x64-linux")
endif()

# Search paths: vcpkg, then system paths
if(WIN32)
    # Windows: Check vcpkg, then common install locations
    set(_SEARCH_PATHS
        "${_VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}"
        "${CMAKE_PREFIX_PATH}"
        "$ENV{OPENSLIDE_HOME}"
        "C:/openslide"
        "C:/Program Files/openslide"
        "C:/Program Files (x86)/openslide"
    )
else()
    # Unix: Check vcpkg, Homebrew, then system paths
    set(_SEARCH_PATHS
        "${_VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}"
        "${CMAKE_PREFIX_PATH}"
        /opt/homebrew
        /usr/local
        /usr
    )
endif()

# Find include directory
find_path(OPENSLIDE_INCLUDE_DIR openslide/openslide.h
    PATHS ${_SEARCH_PATHS}
    PATH_SUFFIXES include)

# Find the library
if(WIN32)
    find_library(OPENSLIDE_LIBRARY
        NAMES openslide libopenslide
        PATHS ${_SEARCH_PATHS}
        PATH_SUFFIXES lib bin)
else()
    find_library(OPENSLIDE_LIBRARY
        NAMES openslide libopenslide
        PATHS ${_SEARCH_PATHS}
        PATH_SUFFIXES lib)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenSlide
    REQUIRED_VARS OPENSLIDE_LIBRARY OPENSLIDE_INCLUDE_DIR)

if(OPENSLIDE_FOUND)
    set(OPENSLIDE_LIBRARIES ${OPENSLIDE_LIBRARY})
    set(OPENSLIDE_INCLUDE_DIRS ${OPENSLIDE_INCLUDE_DIR})
    
    # Create imported target for modern CMake usage
    if(NOT TARGET OpenSlide::OpenSlide)
        add_library(OpenSlide::OpenSlide UNKNOWN IMPORTED)
        set_target_properties(OpenSlide::OpenSlide PROPERTIES
            IMPORTED_LOCATION "${OPENSLIDE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSLIDE_INCLUDE_DIR}"
        )
    endif()
    
    mark_as_advanced(OPENSLIDE_INCLUDE_DIR OPENSLIDE_LIBRARY)
endif()
