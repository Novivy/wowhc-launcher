cmake_minimum_required(VERSION 3.20)

# Inject Windows SDK paths before project() so the MSVC compiler test passes.
# CLion's bare MSVC toolchain doesn't run vcvarsall, so INCLUDE/LIB are empty.
set(_WINSDK  "C:/Program Files (x86)/Windows Kits/10")
set(_SDKVER  "10.0.26100.0")
set(_MSVCVER "14.44.35207")
set(_MSVC    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/${_MSVCVER}")

set(ENV{INCLUDE}
    "${_MSVC}/include;${_WINSDK}/Include/${_SDKVER}/um;${_WINSDK}/Include/${_SDKVER}/shared;${_WINSDK}/Include/${_SDKVER}/ucrt"
)
set(ENV{LIB}
    "${_MSVC}/lib/x64;${_WINSDK}/Lib/${_SDKVER}/um/x64;${_WINSDK}/Lib/${_SDKVER}/ucrt/x64"
)

project(2026_wowhc_launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME} WIN32 main.cpp app.rc)

# /utf-8 so MSVC correctly reads the UTF-8 source file for wide string literals
target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${_MSVC}/include"
    "${_WINSDK}/Include/${_SDKVER}/um"
    "${_WINSDK}/Include/${_SDKVER}/shared"
    "${_WINSDK}/Include/${_SDKVER}/ucrt"
)

target_link_directories(${PROJECT_NAME} PRIVATE
    "${_MSVC}/lib/x64"
    "${_WINSDK}/Lib/${_SDKVER}/um/x64"
    "${_WINSDK}/Lib/${_SDKVER}/ucrt/x64"
)

# We embed our own manifest via app.rc — disable the linker-generated one
target_link_options(${PROJECT_NAME} PRIVATE /MANIFEST:NO)

target_link_libraries(${PROJECT_NAME} PRIVATE
    winhttp
    comctl32
    shlwapi
    shell32
    ole32
    uuid
)
