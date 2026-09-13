cmake_minimum_required(VERSION 3.24)
project(VRCracktroQuest LANGUAGES C CXX)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "Quest build requires Android arm64-v8a")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
if(NOT OPENXR_SOURCE_DIR)
  message(FATAL_ERROR "Pass OPENXR_SOURCE_DIR (build-quest.ps1 downloads the pinned SDK)")
endif()
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_SYSTEM_JSONCPP OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_STD_FILESYSTEM OFF CACHE BOOL "" FORCE)
add_compile_options(-Oz -ffunction-sections -fdata-sections)
add_subdirectory("${OPENXR_SOURCE_DIR}" openxr EXCLUDE_FROM_ALL)
add_library(cracktro SHARED QuestMain.cpp QuestAssets.cpp
  ../src/intro/Pocketmod.c
  "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(cracktro PRIVATE ../third-party/openxr-sdk/include
  "${ANDROID_NDK}/sources/android/native_app_glue")
target_compile_definitions(cracktro PRIVATE XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_OPENGL_ES)
target_compile_options(cracktro PRIVATE -Oz -ffunction-sections -fdata-sections -fvisibility=hidden
  $<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden> -Wall -Wextra -Wno-missing-field-initializers)

target_link_options(cracktro PRIVATE --driver-mode=g++ -Wl,--gc-sections -Wl,--icf=all -Wl,--exclude-libs,ALL
  -Wl,-z,max-page-size=16384 -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
target_link_libraries(cracktro PRIVATE openxr_loader android log EGL GLESv3 aaudio)
