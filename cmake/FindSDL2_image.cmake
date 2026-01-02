# FindSDL2_image.cmake
# Find SDL2_image library
#
# This module defines:
#  SDL2_IMAGE_FOUND - whether SDL2_image was found
#  SDL2_IMAGE_INCLUDE_DIRS - include directories for SDL2_image
#  SDL2_IMAGE_LIBRARIES - libraries to link against SDL2_image
#
# This module also creates the following imported targets:
#  SDL2_image::SDL2_image-static - Static library target

# Prefer vcpkg toolchain settings when available.
if(DEFINED VCPKG_INSTALLED_DIR)
    set(_VCPKG_INSTALLED_DIR "${VCPKG_INSTALLED_DIR}")
elseif(DEFINED ENV{VCPKG_INSTALLED_DIR})
    set(_VCPKG_INSTALLED_DIR "$ENV{VCPKG_INSTALLED_DIR}")
elseif(DEFINED ENV{VCPKG_ROOT})
    set(_VCPKG_INSTALLED_DIR "$ENV{VCPKG_ROOT}/installed")
else()
    set(_VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed")
endif()

# Determine vcpkg triplet
if(DEFINED VCPKG_TARGET_TRIPLET)
    set(_VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
elseif(DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
    set(_VCPKG_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}")
elseif(WIN32)
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

# Search paths: vcpkg installed dir first, then system paths
if(WIN32)
    set(_SEARCH_PATHS
        "${_VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}"
        "${CMAKE_PREFIX_PATH}"
        "$ENV{SDL2_IMAGE_HOME}"
    )
else()
    set(_SEARCH_PATHS
        "${_VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}"
        "${CMAKE_PREFIX_PATH}"
        /opt/homebrew
        /usr/local
        /usr
    )
endif()

# Find include directory
find_path(SDL2_IMAGE_INCLUDE_DIR SDL_image.h
    PATHS ${_SEARCH_PATHS}
    PATH_SUFFIXES include include/SDL2 SDL2)

# Find the library
if(WIN32)
    find_library(SDL2_IMAGE_LIBRARY
        NAMES SDL2_image SDL2_image-static libSDL2_image
        PATHS ${_SEARCH_PATHS}
        PATH_SUFFIXES lib bin)
else()
    find_library(SDL2_IMAGE_LIBRARY
        NAMES SDL2_image SDL2_image-static libSDL2_image
        PATHS ${_SEARCH_PATHS}
        PATH_SUFFIXES lib)
endif()

# Handle the REQUIRED argument and set SDL2_IMAGE_FOUND
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2_image
    REQUIRED_VARS SDL2_IMAGE_LIBRARY SDL2_IMAGE_INCLUDE_DIR)

if(SDL2_IMAGE_FOUND)
    set(SDL2_IMAGE_LIBRARIES ${SDL2_IMAGE_LIBRARY})
    set(SDL2_IMAGE_INCLUDE_DIRS ${SDL2_IMAGE_INCLUDE_DIR})
    
    # Create imported target for compatibility with modern CMake usage
    if(NOT TARGET SDL2_image::SDL2_image-static)
        add_library(SDL2_image::SDL2_image-static STATIC IMPORTED)
        set_target_properties(SDL2_image::SDL2_image-static PROPERTIES
            IMPORTED_LOCATION "${SDL2_IMAGE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_IMAGE_INCLUDE_DIR}"
        )
    endif()
    
    # Also create SDL2_image::SDL2_image as an alias for compatibility
    if(NOT TARGET SDL2_image::SDL2_image)
        add_library(SDL2_image::SDL2_image ALIAS SDL2_image::SDL2_image-static)
    endif()
    
    mark_as_advanced(SDL2_IMAGE_INCLUDE_DIR SDL2_IMAGE_LIBRARY)
endif()
