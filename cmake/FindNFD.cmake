# FindNFD.cmake
# Find nativefiledialog-extended (NFD) library
#
# This module defines:
#  NFD_FOUND - whether NFD was found
#  NFD_INCLUDE_DIRS - include directories for NFD
#  NFD_LIBRARIES - libraries to link against NFD
#
# This module also creates the following imported target:
#  nfd::nfd - The NFD library target

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

# Search paths: vcpkg installed dir first, then system paths
if(WIN32)
    set(_SEARCH_PATHS
        "${_VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}"
        "${CMAKE_PREFIX_PATH}"
        "$ENV{NFD_HOME}"
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

# Find include directory (look for nfd.h)
find_path(NFD_INCLUDE_DIR nfd.h
    PATHS ${_SEARCH_PATHS}
    PATH_SUFFIXES include)

# Find the library
if(WIN32)
    find_library(NFD_LIBRARY
        NAMES nfd libnfd
        PATHS ${_SEARCH_PATHS}
        PATH_SUFFIXES lib bin)
else()
    find_library(NFD_LIBRARY
        NAMES nfd libnfd
        PATHS ${_SEARCH_PATHS}
        PATH_SUFFIXES lib)
endif()

# Handle the REQUIRED argument and set NFD_FOUND
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NFD
    REQUIRED_VARS NFD_LIBRARY NFD_INCLUDE_DIR)

if(NFD_FOUND)
    set(NFD_LIBRARIES ${NFD_LIBRARY})
    set(NFD_INCLUDE_DIRS ${NFD_INCLUDE_DIR})
    
    # Create imported target for compatibility with modern CMake usage
    if(NOT TARGET nfd::nfd)
        add_library(nfd::nfd STATIC IMPORTED)
        set_target_properties(nfd::nfd PROPERTIES
            IMPORTED_LOCATION "${NFD_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${NFD_INCLUDE_DIR}"
        )
        
        # Platform-specific dependencies
        if(WIN32)
            # NFD on Windows requires COM libraries
            set_property(TARGET nfd::nfd APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES comctl32 ole32 uuid shell32
            )
        elseif(APPLE)
            # NFD on macOS requires AppKit framework
            set_property(TARGET nfd::nfd APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES "-framework AppKit" "-framework UniformTypeIdentifiers"
            )
        else()
            # NFD on Linux requires GTK3 or other portal backends
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(GTK3 QUIET gtk+-3.0)
                if(GTK3_FOUND)
                    set_property(TARGET nfd::nfd APPEND PROPERTY
                        INTERFACE_INCLUDE_DIRECTORIES "${GTK3_INCLUDE_DIRS}"
                    )
                    set_property(TARGET nfd::nfd APPEND PROPERTY
                        INTERFACE_LINK_LIBRARIES "${GTK3_LIBRARIES}"
                    )
                endif()
            endif()
        endif()
    endif()
    
    mark_as_advanced(NFD_INCLUDE_DIR NFD_LIBRARY)
endif()
