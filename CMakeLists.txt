cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

project(libOpenDTP
    VERSION 1.0.0
    LANGUAGES CXX
)

# Set C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Options
option(BUILD_SHARED_LIBS "Build libOpenDTP as a shared library" OFF)
option(BUILD_TESTING "Build the test suite" ON)
option(BUILD_EXAMPLES "Build the example application" ON)

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

# Configure Compiler Flags
if(MSVC)
    add_compile_options(/W4 /WX /wd4200) # Disable warning for zero-sized array description[0]
else()
    add_compile_options(-Wall -Wextra -Werror) # Enable warnings as errors
    if(ENABLE_COVERAGE)
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
endif()

# Include FetchContent
include(FetchContent)

# Fetch Standalone ASIO (header-only, no Boost required)
message(STATUS "Fetching Asio (standalone)...")
FetchContent_Declare(
    asio
    URL "https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz"
)
FetchContent_MakeAvailable(asio)

# Fetch GoogleTest (if testing enabled)
if(BUILD_TESTING)
    message(STATUS "Fetching GoogleTest...")
    FetchContent_Declare(
        googletest
        URL "https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"
    )
    # Prevent overriding the parent project's compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Define core library target
add_library(opendtp
    src/Client.cpp
    src/Protocol.cpp
    src/Sha256.cpp
)

# Header include paths
target_include_directories(opendtp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${asio_SOURCE_DIR}/asio/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)

target_link_libraries(opendtp
    PRIVATE
        Threads::Threads
)
target_compile_definitions(opendtp
    PUBLIC
        ASIO_STANDALONE
)

# Add subdirectories for tests and examples
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Installation Rules ---
include(GNUInstallDirs)

install(TARGETS opendtp
    EXPORT libOpenDTPTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install public headers
install(DIRECTORY include/opendtp/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/opendtp
    FILES_MATCHING PATTERN "*.hpp"
)

# --- Doxygen API Documentation ---
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "libOpenDTP")
    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_PROJECT_BRIEF "Open DMR Terminal Protocol C++ Library")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

    doxygen_add_docs(doc
        ${CMAKE_CURRENT_SOURCE_DIR}/include/opendtp/Types.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/opendtp/Common.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/opendtp/Protocol.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/opendtp/Client.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/README.md
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
    )

    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs/html/"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/html"
        OPTIONAL
    )
endif()

# --- Determine Version Suffix based on Git state ---
set(PACKAGE_VERSION_SUFFIX "")
find_package(Git QUIET)
if(GIT_FOUND)
    # Check if we are on an exact release tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_EXACT_TAG_RESULT
        OUTPUT_VARIABLE GIT_EXACT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(NOT GIT_EXACT_TAG_RESULT EQUAL 0)
        # Not on a release tag, try to append the git short commit hash
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE GIT_REF_RESULT
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GIT_REF_RESULT EQUAL 0)
            set(PACKAGE_VERSION_SUFFIX "-git-${GIT_COMMIT_HASH}")
        else()
            set(PACKAGE_VERSION_SUFFIX "-git-dev")
        endif()
    endif()
else()
    set(PACKAGE_VERSION_SUFFIX "-dev")
endif()

# --- Packaging Configuration (CPack) ---
set(CPACK_PACKAGE_NAME "libopendtp")
set(CPACK_PACKAGE_VENDOR "Rui Barreiros")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open DMR Terminal Protocol library")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}${PACKAGE_VERSION_SUFFIX}")
set(CPACK_PACKAGE_CONTACT "rbarreiros@gmail.com")

# System-dependent generators
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;TGZ;ZIP")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "rbarreiros@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
elseif(APPLE)
    set(CPACK_GENERATOR "productbuild;TGZ;ZIP")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
endif()

include(CPack)
