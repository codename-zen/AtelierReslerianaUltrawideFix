cmake_minimum_required(VERSION 3.25)
project(AtelierReslerianaFix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The game is 64-bit only; configure with a x64 toolchain.")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG v0.6.4)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.1)
# inipp is header-only and its own CMakeLists predates CMake 4, so point
# SOURCE_SUBDIR at a directory without one: the sources are fetched, but no
# add_subdirectory() is attempted.
FetchContent_Declare(inipp
    GIT_REPOSITORY https://github.com/mcmtroffaes/inipp.git
    GIT_TAG master
    SOURCE_SUBDIR inipp)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(safetyhook spdlog inipp)

add_library(${PROJECT_NAME} SHARED
    src/dllmain.cpp
    src/anchors.cpp
    src/bootstrap.cpp
    src/elements.cpp
    src/config.cpp
    src/il2cpp_api.cpp
    src/unity_bindings.cpp
    src/fixes.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${inipp_SOURCE_DIR}/inipp)

target_link_libraries(${PROJECT_NAME} PRIVATE safetyhook spdlog::spdlog)

# Ultimate ASI Loader picks the plugin up by extension, exactly like AtelierYumiaFix.
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "AtelierReslerianaFix"
    SUFFIX ".asi")

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
