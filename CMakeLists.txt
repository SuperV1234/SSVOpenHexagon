cmake_minimum_required(VERSION 3.9)

#
#
# -----------------------------------------------------------------------------
# Colored output
# -----------------------------------------------------------------------------

# From: https://medium.com/@alasher/colored-c-compiler-output-with-ninja-clang-gcc-10bfe7f2b949
option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." FALSE)

if(${FORCE_COLORED_OUTPUT})
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options("-fdiagnostics-color=always")
    elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       add_compile_options("-fcolor-diagnostics")
    endif()
endif()

#
#
# -----------------------------------------------------------------------------
# CCache
# -----------------------------------------------------------------------------

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Found CCACHE")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

#
#
# -----------------------------------------------------------------------------
# CPM
# -----------------------------------------------------------------------------

set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
set(CPM_DOWNLOAD_VERSION 0.25.1)

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION} AND CPM_VERSION STREQUAL CPM_DOWNLOAD_VERSION))
    message(STATUS "Downloading CPM.cmake")
    file(DOWNLOAD https://github.com/TheLartians/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake ${CPM_DOWNLOAD_LOCATION})
endif()

include(${CPM_DOWNLOAD_LOCATION})

#
#
# -----------------------------------------------------------------------------
# CPM: SFML
# -----------------------------------------------------------------------------

set(BUILD_SHARED_LIBS true)
set(SFML_STATIC_LIBRARIES false)

CPMAddPackage(
    NAME SFML
    GITHUB_REPOSITORY SuperV1234/SFML
    GIT_TAG 4e72e5d5bfa53310050b99bf69f73c5ff86419ac
)

set_target_properties(sfml-system PROPERTIES UNITY_BUILD ON)
set_target_properties(sfml-window PROPERTIES UNITY_BUILD OFF)
set_target_properties(sfml-graphics PROPERTIES UNITY_BUILD OFF)
set_target_properties(sfml-audio PROPERTIES UNITY_BUILD ON)
set_target_properties(sfml-network PROPERTIES UNITY_BUILD ON)

#
#
# -----------------------------------------------------------------------------
# CPM: luajit
# -----------------------------------------------------------------------------

# TODO (P2): revisit this decision about FFI
set(LUAJIT_DISABLE_FFI true)
set(LUAJIT_DISABLE_FFI ON CACHE BOOL "" FORCE)

CPMAddPackage(
    NAME luajit
    GIT_REPOSITORY https://github.com/SuperV1234/LuaJIT
    GIT_TAG 0591e7385dd6b3e346ef3a420561f5115da2ddf7
)

set(LUAJIT_DISABLE_FFI true)
set(LUAJIT_DISABLE_FFI ON CACHE BOOL "" FORCE)

# Remove linking against libm on MinGW
if(WIN32)
    file(READ "${luajit_SOURCE_DIR}/src/CMakeLists.txt" CONTENTS)
    string(REGEX REPLACE "target_link_libraries\\(libluajit \\\${LIBM_LIBRARIES}\\)" "" STRIPPED1 "${CONTENTS}")
    file(WRITE "${luajit_SOURCE_DIR}/src/CMakeLists.txt" "${STRIPPED1}")
endif()

#
#
# -----------------------------------------------------------------------------
# CPM: zlib
# -----------------------------------------------------------------------------

CPMAddPackage(
    NAME zlib
    GITHUB_REPOSITORY madler/zlib
    GIT_TAG cacf7f1d4e3d44d871b605da3b647f07d718623f
)

# Remove example binaries from CMakeLists
file(READ "${zlib_SOURCE_DIR}/CMakeLists.txt" CONTENTS)
string(REGEX REPLACE "Example binaries.*" "" STRIPPED1 "${CONTENTS}")
file(WRITE "${zlib_SOURCE_DIR}/CMakeLists.txt" "${STRIPPED1}")

#
#
# -----------------------------------------------------------------------------
# CPM: imgui
# -----------------------------------------------------------------------------

CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG 393941ceea61e1307ffad8e3a045ab384d1299bd
    DOWNLOAD_ONLY YES
)

if (imgui_ADDED)
    # imgui has no CMake support, so we create our own target

    FILE(GLOB imgui_sources ${imgui_SOURCE_DIR}/*.cpp)
    FILE(GLOB imgui_sources_cpp ${imgui_SOURCE_DIR}/misc/cpp/*.cpp)

    set(IMGUI_DIR ${imgui_SOURCE_DIR})
    set(IMGUI_INCLUDE_DIR ${imgui_SOURCE_DIR})
    set(IMGUI_SOURCES ${imgui_sources} ${imgui_sources_cpp})
endif()

#
#
# -----------------------------------------------------------------------------
# CPM: imgui-sfml
# -----------------------------------------------------------------------------

CPMAddPackage(
    NAME imgui-sfml
    GITHUB_REPOSITORY eliasdaler/imgui-sfml
    GIT_TAG 82dc2033e51b8323857c3ae1cf1f458b3a933c35
    )

set_target_properties(ImGui-SFML PROPERTIES UNITY_BUILD ON)

#
#
# -----------------------------------------------------------------------------
# CPM: libsodium-cmake
# -----------------------------------------------------------------------------

set(SODIUM_DISABLE_TESTS ON)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)

CPMAddPackage(
    NAME libsodium-cmake
    GITHUB_REPOSITORY robinlinden/libsodium-cmake
    GIT_TAG a8ac4509b22b84d6c2eb7d7448f08678e4a67da6
    OPTIONS "SODIUM_DISABLE_TESTS ON" "SODIUM_DISABLE_TESTS ON CACHE BOOL \"\" FORCE"
)

set(SODIUM_DISABLE_TESTS ON)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)

#
#
# -----------------------------------------------------------------------------
# CPM: Boost.PFR
# -----------------------------------------------------------------------------

CPMAddPackage(
    NAME boostpfr
    GITHUB_REPOSITORY boostorg/pfr
    GIT_TAG ed82d60966436eefc8fce3ad9b87ab056a2321fd
    DOWNLOAD_ONLY YES
)

if (boostpfr_ADDED)
    set(boostpfr_INCLUDE_DIR ${boostpfr_SOURCE_DIR}/include)
endif()

#
#
# -----------------------------------------------------------------------------
# vrm_cmake
# -----------------------------------------------------------------------------

# Include `vrm_cmake`.
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_SOURCE_DIR}/cmake/"
    "${CMAKE_SOURCE_DIR}/cmake/modules/"
    "${CMAKE_SOURCE_DIR}/../vrm_cmake/cmake/"
    "${CMAKE_SOURCE_DIR}/extlibs/vrm_cmake/cmake/")

include(vrm_cmake)

#
#
# -----------------------------------------------------------------------------
# C++ Standard
# -----------------------------------------------------------------------------

message(STATUS "setting required C++ standard to 20")
set(CMAKE_CXX_STANDARD "20")
set(CMAKE_CXX_STANDARD_REQUIRED on)

#
#
# -----------------------------------------------------------------------------
# Project setup
# -----------------------------------------------------------------------------

project(SSVOpenHexagon CXX C)
enable_testing()

set(PROJECT_NAME_UPPER "")
string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPER)

set("${PROJECT_NAME_UPPER}_SOURCE_DIR" "${CMAKE_CURRENT_SOURCE_DIR}")

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

message(STATUS "initialized project ${PROJECT_NAME}")

#
#
# -----------------------------------------------------------------------------
# Global unity builds
# -----------------------------------------------------------------------------

set(CMAKE_UNITY_BUILD ON)

#
#
# -----------------------------------------------------------------------------
# Source and includes
# -----------------------------------------------------------------------------

set(INC_DIR "include" CACHE STRING "")
set(SRC_DIR "src" CACHE STRING "")

include_directories("./")
include_directories("./${INC_DIR}")

file(GLOB_RECURSE MAIN_FILE "${SRC_DIR}/SSVOpenHexagon/*/main.cpp")

file(GLOB_RECURSE SRC_LIST
     "${INC_DIR}/SSVOpenHexagon/*"
     "${SRC_DIR}/SSVOpenHexagon/*"
     "public/discord/*")

file(GLOB_RECURSE C_SRC_LIST
     "public/sqlite/*.c")

list(REMOVE_ITEM SRC_LIST ${MAIN_FILE})

set_source_files_properties("public/sqlite/sqlite3.c" PROPERTIES COMPILE_FLAGS "-w")
set_source_files_properties("public/sqlite/shell.c" PROPERTIES COMPILE_FLAGS "-w")

#
#
# -----------------------------------------------------------------------------
# Compile commands
# -----------------------------------------------------------------------------

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

#
#
# -----------------------------------------------------------------------------
# Flags and extlibs
# -----------------------------------------------------------------------------

vrm_cmake_include_vc_dependency_once(vc_detection)

# TODO: decide how to handle flags
# vrm_cmake_add_common_compiler_flags()

#
#
# -----------------------------------------------------------------------------
# LTO
# -----------------------------------------------------------------------------

include(CheckIPOSupported)
check_ipo_supported(RESULT result)

if(result)
    if("${vrm_cmake_build_type_lower}" STREQUAL "release")
        message(STATUS "Enabling LTO")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
else()
    message(STATUS "LTO not supported")
endif()

#
#
# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

add_library(SSVOpenHexagonLibC STATIC ${C_SRC_LIST})
add_library(SSVOpenHexagonLib STATIC ${SRC_LIST})
add_executable(SSVOpenHexagon ${MAIN_FILE})

#
#
# -----------------------------------------------------------------------------
# PCH
# -----------------------------------------------------------------------------

target_precompile_headers(
    SSVOpenHexagonLib PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SSVOpenHexagon/Global/PCH.hpp"
)

target_precompile_headers(SSVOpenHexagon REUSE_FROM SSVOpenHexagonLib)

#
#
# -----------------------------------------------------------------------------
# Link and include libs
# -----------------------------------------------------------------------------

set(PUBLIC_INCLUDE_DIRS "public" "public/sqlite" "public/sqlite_orm")

set(SFML_LIBRARIES
    sfml-system sfml-window sfml-graphics sfml-audio sfml-network)

if(WIN32)
    set(PUBLIC_LIBRARIES
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/steam_api64.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/discord_game_sdk.dll.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/sdkencryptedappticket64.lib"
        sodium
    )
elseif(APPLE)
    set(PUBLIC_LIBRARIES
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/libsteam_api.dylib"
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/discord_game_sdk.dylib"
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/libsdkencryptedappticket.dylib"
        sodium
    )
else()
    set(PUBLIC_LIBRARIES
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/libsteam_api.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/libdiscord_game_sdk.so"
        "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/libsdkencryptedappticket.so"
        Xcursor
        sodium
    )
endif()

target_include_directories(
    SSVOpenHexagonLib SYSTEM PUBLIC ${SFML_SOURCE_DIR}/include
                             PUBLIC ${PUBLIC_INCLUDE_DIRS}
                             PUBLIC ${zlib_SOURCE_DIR}
                             PUBLIC ${zlib_BINARY_DIR}
                             PUBLIC ${luajit_SOURCE_DIR}/src
                             PUBLIC ${imgui_SOURCE_DIR}
                             PUBLIC ${boostpfr_INCLUDE_DIR}
)

target_link_libraries(
    SSVOpenHexagonLib ${SFML_LIBRARIES}
                      libluajit
                      zlib
                      ${PUBLIC_LIBRARIES}
                      ImGui-SFML::ImGui-SFML
)

macro(ssvoh_find_extlib_for_target target extlib)
    list(APPEND CMAKE_MODULE_PATH
        "${CMAKE_SOURCE_DIR}/../${extlib}/cmake/modules/"
        "${CMAKE_SOURCE_DIR}/../${extlib}/cmake/"
        "${CMAKE_SOURCE_DIR}/extlibs/${extlib}/cmake/modules/"
        "${CMAKE_SOURCE_DIR}/extlibs/${extlib}/cmake/")

    find_package("${extlib}" REQUIRED)
    string(TOUPPER "${extlib}" ${extlib}_UPPER)

    target_include_directories(${target} PUBLIC "${${${extlib}_UPPER}_INCLUDE_DIR}")
endmacro()

ssvoh_find_extlib_for_target(SSVOpenHexagonLib vrm_pp)
ssvoh_find_extlib_for_target(SSVOpenHexagonLib SSVUtils)
ssvoh_find_extlib_for_target(SSVOpenHexagonLib SSVMenuSystem)
ssvoh_find_extlib_for_target(SSVOpenHexagonLib SSVStart)

target_link_libraries(SSVOpenHexagon SSVOpenHexagonLib SSVOpenHexagonLibC)

target_include_directories(
    SSVOpenHexagon SYSTEM PUBLIC ${SFML_SOURCE_DIR}/include
                          PUBLIC ${PUBLIC_INCLUDE_DIRS}
                          PUBLIC ${zlib_SOURCE_DIR}
                          PUBLIC ${zlib_BINARY_DIR}
                          PUBLIC ${LUASRC}
                          PUBLIC ${imgui_SOURCE_DIR}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(SSVOpenHexagonLib pthread)
endif()

install(
    TARGETS SSVOpenHexagon RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/_RELEASE/
)

#
#
# -----------------------------------------------------------------------------
# Workshop uploader tool
# -----------------------------------------------------------------------------

if(WIN32)
    set(STEAM_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/steam_api64.lib")
elseif(APPLE)
    set(STEAM_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/libsteam_api.dylib")
else()
    set(STEAM_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/_RELEASE/libsteam_api.so")
endif()

add_executable(
    OHWorkshopUploader "${CMAKE_CURRENT_SOURCE_DIR}/src/OHWorkshopUploader/main.cpp"
)

target_include_directories(
    OHWorkshopUploader SYSTEM PUBLIC ${PUBLIC_INCLUDE_DIRS}
)

target_link_libraries(OHWorkshopUploader ${STEAM_LIBRARIES})

if(UNIX AND NOT APPLE)
    target_link_libraries(OHWorkshopUploader pthread)
endif()

install(
    TARGETS OHWorkshopUploader RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/_RELEASE/
)

#
#
# -----------------------------------------------------------------------------
# Server control tool
# -----------------------------------------------------------------------------

add_executable(
    OHServerControl "${CMAKE_CURRENT_SOURCE_DIR}/src/OHServerControl/main.cpp"
)

target_include_directories(
    OHServerControl SYSTEM PUBLIC ${SFML_SOURCE_DIR}/include
)

target_link_libraries(OHServerControl sfml-system sfml-network)

if(UNIX AND NOT APPLE)
    target_link_libraries(OHServerControl pthread)
endif()

install(
    TARGETS OHServerControl RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/_RELEASE/
)

#
#
# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------

vrm_check_target()
add_subdirectory(test)
