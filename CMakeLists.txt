cmake_minimum_required(VERSION 3.12)
project(Quarksql VERSION 0.1 LANGUAGES CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable threading
find_package(Threads REQUIRED)

# Prefer a local copy of Boost.System from libs/thirdparties
set(_LOCAL_BOOST_LIBDIR "${PROJECT_SOURCE_DIR}/libs/thirdparties")
set(_LOCAL_BOOST_SYSTEM_CANDIDATES
    "${_LOCAL_BOOST_LIBDIR}/libboost_system.so"
    "${_LOCAL_BOOST_LIBDIR}/libboost_system.so.1.69.0"
    "${_LOCAL_BOOST_LIBDIR}/libboost_system.dylib"
    "${_LOCAL_BOOST_LIBDIR}/boost_system.lib"
)
foreach(_cand IN LISTS _LOCAL_BOOST_SYSTEM_CANDIDATES)
    if(EXISTS "${_cand}")
        add_library(Boost::system UNKNOWN IMPORTED)
        set_target_properties(Boost::system PROPERTIES IMPORTED_LOCATION "${_cand}")
        # Use local Boost headers if present
        if(EXISTS "${PROJECT_SOURCE_DIR}/third-party/includes/boost")
            set_target_properties(Boost::system PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/third-party/includes")
        endif()
        set(Boost_FOUND TRUE)
        set(_QUARKSQL_USING_LOCAL_BOOST TRUE)
        message(STATUS "Using local Boost::system from ${_cand}")
        break()
    endif()
endforeach()

# If no local copy, try to find Boost 1.69.0 in the system
if(NOT Boost_FOUND)
    set(Boost_USE_STATIC_LIBS OFF)
    find_package(Boost 1.69.0 EXACT COMPONENTS system QUIET)
endif()

# If Boost still not found, download and build it
if(NOT Boost_FOUND)
    include(ExternalProject)
    ExternalProject_Add(
        boost_ext
        URL "https://sourceforge.net/projects/boost/files/boost/1.69.0/boost_1_69_0.tar.gz/download"
        URL_HASH SHA256=9914f2f6ddd1b72818b57bde40e95c4fa0d5bd3ba2b4ecf04b6ae744c3b8c8ef
        DOWNLOAD_NAME boost_1_69_0.tar.gz
        SOURCE_DIR "${CMAKE_BINARY_DIR}/boost/src"
        BINARY_DIR "${CMAKE_BINARY_DIR}/boost/build"
        CONFIGURE_COMMAND "${CMAKE_BINARY_DIR}/boost/src/bootstrap.sh" "--prefix=${CMAKE_BINARY_DIR}/boost/install"
        BUILD_COMMAND   "${CMAKE_BINARY_DIR}/boost/src/b2" install
        INSTALL_DIR     "${CMAKE_BINARY_DIR}/boost/install"
    )
    
    # Configure CMake to find the newly built Boost
    set(BOOST_ROOT     "${CMAKE_BINARY_DIR}/boost/install" CACHE PATH "Boost root directory")
    set(CMAKE_PREFIX_PATH ${BOOST_ROOT} ${CMAKE_PREFIX_PATH})
    find_package(Boost 1.69.0 EXACT COMPONENTS system REQUIRED)
endif()

# Find RocksDB manually
find_path(ROCKSDB_INCLUDE_DIR
    NAMES rocksdb/db.h
    HINTS /usr/include /usr/local/include
)
find_library(ROCKSDB_LIBRARY
    NAMES rocksdb
    HINTS /usr/lib /usr/local/lib
)
if(NOT ROCKSDB_INCLUDE_DIR OR NOT ROCKSDB_LIBRARY)
    message(FATAL_ERROR "RocksDB not found. Please install librocksdb-dev.")
endif()

# Include directories for headers (prefer local Boost headers first)
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third-party/crow/include
    ${PROJECT_SOURCE_DIR}/third-party/includes
    ${Boost_INCLUDE_DIRS}
    ${ROCKSDB_INCLUDE_DIR}
     /usr/include/v8
)


# Gather all .cpp sources
file(GLOB QUARKSQL_SOURCES
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# Define executable and ensure Boost is built first if needed
add_executable(quarksql ${QUARKSQL_SOURCES})
if(TARGET boost_ext)
    add_dependencies(quarksql boost_ext)
endif()

# Ensure the runtime can locate local third-party libs during build/run
set_target_properties(quarksql PROPERTIES BUILD_RPATH "${PROJECT_SOURCE_DIR}/libs/thirdparties")

# Link against Boost.System, RocksDB, OpenSSL, and pthread
target_link_libraries(quarksql PRIVATE
    Boost::system
    ${ROCKSDB_LIBRARY}
    ssl
    crypto
    Threads::Threads
    -lv8
)
