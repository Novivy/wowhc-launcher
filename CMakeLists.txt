cmake_minimum_required(VERSION 3.20)

# Inject Windows SDK paths before project() so the MSVC compiler test passes.
# CLion's bare MSVC toolchain doesn't run vcvarsall, so INCLUDE/LIB are empty.
# Skip when CI=true — GitHub Actions' ilammy/msvc-dev-cmd already sets these.
if(NOT DEFINED ENV{CI})
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
  # Set RC/MT with forward-slash paths before project() so CMake doesn't
  # auto-detect them with backslashes — CMake 3.31 treats \P as invalid escape.
  set(CMAKE_RC_COMPILER "${_WINSDK}/bin/${_SDKVER}/x64/rc.exe" CACHE FILEPATH "" FORCE)
  set(CMAKE_MT          "${_WINSDK}/bin/${_SDKVER}/x64/mt.exe" CACHE FILEPATH "" FORCE)
  set(_HAS_LOCAL_PATHS TRUE)
endif()

project(2026_wowhc_launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")

add_executable(${PROJECT_NAME} WIN32
    main.cpp
    replay_buffer.cpp
    replay_ui.cpp
    app.rc
)

# Static runtime: /MT for Release, /MTd for Debug — no VCRUNTIME DLL dependency
set_property(TARGET ${PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# /utf-8 so MSVC correctly reads the UTF-8 source file for wide string literals
target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)

# Release: optimize for size+speed, enable whole-program optimization
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/O2 /GL /GS->
)
target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
)

if(DEFINED LAUNCHER_VERSION)
  set(_VER_STR "${LAUNCHER_VERSION}")
else()
  set(_VER_STR "v0.0.0-dev")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    LAUNCHER_VERSION_STR="${_VER_STR}"
)

target_include_directories(${PROJECT_NAME} PRIVATE "${FFMPEG_DIR}/include")
# FFmpeg libs are loaded dynamically via LoadLibrary; no import libs needed

if(_HAS_LOCAL_PATHS)
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
endif()

# We embed our own manifest via app.rc — disable the linker-generated one
target_link_options(${PROJECT_NAME} PRIVATE /MANIFEST:NO)

target_link_libraries(${PROJECT_NAME} PRIVATE
    winhttp
    comctl32
    shlwapi
    shell32
    ole32
    uuid
    gdiplus
    d3d11
    dxgi
)

# Copy FFmpeg DLLs next to the built EXE
file(GLOB FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FFMPEG_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying FFmpeg DLLs to output directory"
)
