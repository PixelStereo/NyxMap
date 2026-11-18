cmake_minimum_required(VERSION 3.21)
include(cmake/Version.cmake)
project(NyxMap VERSION ${NYXMAP_VERSION_NUMERIC} LANGUAGES C CXX) # C: ufbx.c
configure_file(cmake/Version.h.in generated/NyxMapVersion.h @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(MSVC)
  add_compile_options(/utf-8 /W3)   # UTF-8 sources (UI strings contain non-ASCII characters)
  add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()
# Warnings on NyxMap's own code (the libraries fetched below keep theirs)
set(NYXMAP_WARNINGS $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wno-unused-parameter>)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Network)
# Shared Pixel Stereo code (the OSC codec), pinned as a submodule
set(PXS_BUILD_TESTS OFF)
add_subdirectory(third_party/pxs EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# FFmpeg: pkg-config (macOS Homebrew, Linux) or FFMPEG_ROOT (Windows, "shared" build)
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg folder containing include/ and lib/ (Windows)")
set(FFMPEG_LIBS avformat avcodec swscale swresample avutil)

if(FFMPEG_ROOT)
  add_library(ffmpeg INTERFACE)
  target_include_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
  foreach(lib ${FFMPEG_LIBS})
    find_library(FF_${lib} NAMES ${lib} lib${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(ffmpeg INTERFACE ${FF_${lib}})
  endforeach()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
  add_library(ffmpeg INTERFACE)
  target_link_libraries(ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

# ---------------------------------------------------------------------------
# Engine: no dependency on widgets (reusable with another UI)
# ---------------------------------------------------------------------------
add_library(nyxmap_engine STATIC
  src/engine/Model3D.cpp
  src/engine/Path3D.cpp
  src/engine/MaterialX.cpp
  third_party/ufbx/ufbx.c
  src/engine/Isf.cpp
  src/engine/LineGenerator.cpp
  src/engine/ImageFile.cpp
  src/engine/Psd.cpp
  src/engine/IsfLibrary.cpp
  src/engine/Mapping.cpp
  src/engine/VideoBudget.cpp
  src/engine/VideoDecoder.cpp
  src/engine/VideoDecoderHap.cpp
  src/engine/VideoDecoderDelivery.cpp
  src/engine/VideoDecoderLayout.cpp
  src/engine/VideoDecoderDemux.cpp
  src/engine/VideoDecoderSeek.cpp
  src/engine/VideoDecoderMetrics.cpp
  src/engine/ProcessMemory.cpp
  src/engine/Hap.cpp
  src/engine/Snappy.cpp
  src/engine/WorkerPool.cpp
  src/engine/Engine.cpp
  src/engine/Layers.cpp
  src/engine/Render.cpp
  src/engine/Project.cpp
  src/engine/ProjectCollect.cpp
  src/engine/Media.cpp
  src/engine/Publish.cpp
  src/engine/DeckLink.cpp
  src/engine/Midi.cpp
  src/engine/LiveInput.cpp
  src/engine/Tox.cpp
  src/engine/ToxTouchEngine.cpp
  src/engine/WebPage.cpp
  src/engine/Permissions.cpp
  src/engine/FaustInterpreter.cpp
  src/engine/AudioEffects.cpp
  src/engine/AudioEffectEngine.cpp
  src/engine/AudioGenerator.cpp
  src/engine/BeatClock.cpp
  src/engine/Controls.cpp
  src/engine/RemoteServer.cpp
  src/engine/AudioCapture.cpp
  src/engine/AudioCaptureDevice.cpp
  src/engine/AudioStream.cpp
  src/engine/AudioOutput.cpp
  src/engine/AudioInput.cpp
  src/engine/MiniaudioImpl.cpp
  src/engine/Osc.cpp
  src/engine/OscShowControl.cpp
  src/engine/ShowDesk.cpp
  src/engine/EventLog.cpp
  src/engine/FrameTiming.cpp
  src/engine/SequenceFeedback.cpp
  src/engine/Zeroconf.cpp
  src/engine/StoredContent.cpp
  src/engine/Snapshots.cpp
  src/engine/Sequencer.cpp
  src/engine/Publishing.cpp
  src/engine/Fixtures.cpp
  src/engine/Dmx.cpp
  src/engine/PixelSampler.cpp
  src/engine/TestCard.cpp
  src/engine/PixelMapping.cpp
  src/engine/Zip.cpp
  src/engine/Gdtf.cpp
  src/engine/Masks.cpp
  src/engine/Timecode.cpp
  src/engine/TimecodeClock.cpp
  src/engine/TimecodeIo.cpp
  src/engine/TimecodeOutputs.cpp
  src/engine/ClockGenerator.cpp
  src/engine/TextWriting.cpp
  src/engine/Interactions.cpp
  src/engine/InteractionsEngine.cpp
  src/engine/Presets.cpp
  src/engine/Animations.cpp
  src/engine/ShowParams.cpp
  src/engine/LayerParams.cpp
)
target_include_directories(nyxmap_engine PUBLIC src/engine)
target_include_directories(nyxmap_engine SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/faust/architecture")
# Built-in Faust instruments use portable generated C++; the optional interpreter
# enables arbitrary user .dsp files without any compilation in the audio callback.
option(NYXMAP_FAUST_COMPILER "Compile user Faust .dsp generators with libfaust" ON)
if(NYXMAP_FAUST_COMPILER)
  find_path(FAUST_COMPILER_INCLUDE_DIR faust/dsp/interpreter-dsp.h)
  find_library(FAUST_COMPILER_LIBRARY NAMES faust libfaust)
  find_path(FAUST_LIBRARY_PATH stdfaust.lib PATH_SUFFIXES share/faust faust)
  if(FAUST_COMPILER_INCLUDE_DIR AND FAUST_COMPILER_LIBRARY AND FAUST_LIBRARY_PATH)
    target_include_directories(nyxmap_engine SYSTEM PRIVATE "${FAUST_COMPILER_INCLUDE_DIR}")
    target_compile_definitions(nyxmap_engine PUBLIC NYXMAP_FAUST_COMPILER)
    target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_FAUST_LIBRARY_PATH="${FAUST_LIBRARY_PATH}" NYXMAP_FAUST_LIBRARY_FILE="${FAUST_COMPILER_LIBRARY}")
  else()
    message(STATUS "libfaust compiler not found: built-in audio instruments available; user .dsp loading disabled")
  endif()
endif()

# QRhi (Metal, Direct3D, Vulkan) and its shaders, baked at run time (ISF)
set(QT_NO_PRIVATE_MODULE_WARNING ON) # QRhi is semi-public: Qt pinned, the renderer isolated behind it
# Up to Qt 6.8 the private modules come with their public ones; from 6.9 they are packages of their own (on Linux
# distributions: qt6-base-private-dev)
find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
foreach(private GuiPrivate ShaderToolsPrivate)
  if(NOT TARGET Qt6::${private})
    find_package(Qt6 QUIET COMPONENTS ${private})
  endif()
  if(NOT TARGET Qt6::${private})
    message(FATAL_ERROR "Qt6::${private} not found: QRhi needs Qt's private headers (qt6-base-private-dev on Debian and Ubuntu)")
  endif()
endforeach()
target_sources(nyxmap_engine PRIVATE src/engine/RhiDevice.cpp src/engine/RhiVideoTexture.cpp src/engine/RhiTarget.cpp
  src/engine/RhiResources.cpp src/engine/IsfRhi.cpp src/engine/RhiRenderer.cpp src/engine/RhiScene.cpp)
# The engine's own shaders, baked at build time for every backend (SPIR-V, MSL, HLSL)
# Direct3D compiles HLSL at run time unless the bytecode is in the shader: the big lit 3D shaders took seconds each on the
# CI runners (and every test makes its own device). With fxc at hand (the Windows SDK), they are compiled here, so
# that an error in one fails the build instead of drawing nothing.
set(NYXMAP_PRECOMPILE "")
if(WIN32)
  find_program(NYXMAP_FXC fxc HINTS "$ENV{WindowsSdkVerBinPath}/x64" "$ENV{WindowsSdkBinPath}/x64")
  if(NYXMAP_FXC)
    set(NYXMAP_PRECOMPILE PRECOMPILE)
    message(STATUS "Direct3D shaders precompiled with ${NYXMAP_FXC}")
  else()
    message(WARNING "fxc not found: the Direct3D shaders are compiled at run time")
  endif()
endif()
qt_add_shaders(nyxmap_engine "nyxmap_shaders"
  PREFIX "/shaders"
  ${NYXMAP_PRECOMPILE}
  NOGLSL # SPIR-V (Vulkan), MSL (Metal), HLSL (Direct3D)
  BASE src/engine/shaders
  FILES src/engine/shaders/videoconvert.vert src/engine/shaders/videoconvert.frag
        src/engine/shaders/blit.vert src/engine/shaders/blit.frag src/engine/shaders/fullscreen.vert
        src/engine/shaders/composite.vert src/engine/shaders/composite.frag src/engine/shaders/prep.frag
        src/engine/shaders/effectmask.frag src/engine/shaders/outputcolor.frag
        src/engine/shaders/shapemask.frag src/engine/shaders/matte.frag src/engine/shaders/compositediff.frag src/engine/shaders/fade.frag src/engine/shaders/testcard.frag src/engine/shaders/softedge.frag
        src/engine/shaders/sceneview.vert src/engine/shaders/sceneplain.frag src/engine/shaders/sceneunlit.frag
        src/engine/shaders/scenelit.frag src/engine/shaders/scenepost.vert src/engine/shaders/scenepost.frag
        src/engine/shaders/sceneshadow.frag src/engine/shaders/scenepoint.frag
        src/engine/shaders/scenedepth.frag src/engine/shaders/sceneao.frag src/engine/shaders/sceneaoblur.frag
        src/engine/shaders/scenelit_noglow.frag src/engine/shaders/sceneglowdown.frag src/engine/shaders/sceneglowup.frag
        src/engine/shaders/scenebackground.frag src/engine/shaders/scenesky.vert src/engine/shaders/scenesky.frag
        src/engine/shaders/scenesky_noglow.frag src/engine/shaders/scenefog.frag src/engine/shaders/scenefogblur.frag)
target_link_libraries(nyxmap_engine PUBLIC Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::ShaderToolsPrivate Qt6::Network ffmpeg pxs::osc pxs::params pxs::tree)
if(WIN32)
  target_link_libraries(nyxmap_engine PRIVATE psapi pdh) # resource usage (ProcessMemory.cpp)
endif()
# Cameras (live inputs): Qt Multimedia when it is there
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(Qt6Multimedia_FOUND)
  target_link_libraries(nyxmap_engine PRIVATE Qt6::Multimedia)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_CAMERA)
else()
  message(STATUS "Qt Multimedia not found: no camera inputs")
endif()
# SVG drawings as images: Qt SVG when it is there
find_package(Qt6 QUIET COMPONENTS Svg)
if(Qt6Svg_FOUND)
  target_link_libraries(nyxmap_engine PRIVATE Qt6::Svg)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_SVG)
else()
  message(STATUS "Qt SVG not found: no SVG images")
endif()

# ---------------------------------------------------------------------------
# Third-party sources: git submodules in third_party/ (git submodule update --init), built here
# ---------------------------------------------------------------------------
function(nyxmap_submodule name file)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/${name}/${file}")
    message(FATAL_ERROR "third_party/${name} is empty: run  git submodule update --init")
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Audio output: miniaudio (single header, public domain / MIT-0), compiled in src/engine/MiniaudioImpl.cpp
# ---------------------------------------------------------------------------
nyxmap_submodule(faust architecture/faust/dsp/dsp.h)
nyxmap_submodule(miniaudio miniaudio.h)
# 3D models: ufbx (FBX, OBJ; MIT / public domain) and cgltf (glTF, GLB; MIT), single files (src/engine/Model3D.cpp)
nyxmap_submodule(ufbx ufbx.c)
nyxmap_submodule(cgltf cgltf.h)
target_include_directories(nyxmap_engine PRIVATE "${CMAKE_SOURCE_DIR}/third_party/ufbx" "${CMAKE_SOURCE_DIR}/third_party/cgltf")
target_include_directories(nyxmap_engine PRIVATE "${CMAKE_SOURCE_DIR}/third_party/miniaudio")
target_compile_definitions(nyxmap_engine PRIVATE MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_ENGINE
  MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH)
if(MSVC)
  set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(APPLE)
  # Link the Core Audio frameworks directly rather than loading them at runtime
  target_compile_definitions(nyxmap_engine PRIVATE MA_NO_RUNTIME_LINKING)
  target_link_libraries(nyxmap_engine PRIVATE "-framework CoreFoundation" "-framework CoreAudio"
    "-framework AudioToolbox")
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(nyxmap_engine PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ---------------------------------------------------------------------------
# MIDI (the timecode's MTC): RtMidi (MIT), a submodule. CoreMIDI, Windows MM, ALSA (Linux: libasound2-dev; without it,
# no MIDI port)
# ---------------------------------------------------------------------------
nyxmap_submodule(rtmidi RtMidi.cpp)
set(rtmidi_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/rtmidi")
set(NYXMAP_MIDI_API "")
if(APPLE)
  set(NYXMAP_MIDI_API __MACOSX_CORE__)
  set(NYXMAP_MIDI_LIBS "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
elseif(WIN32)
  set(NYXMAP_MIDI_API __WINDOWS_MM__)
  set(NYXMAP_MIDI_LIBS winmm)
else()
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    set(NYXMAP_MIDI_API __LINUX_ALSA__)
    set(NYXMAP_MIDI_LIBS ALSA::ALSA Threads::Threads)
  else()
    message(STATUS "ALSA not found: no MIDI ports (sudo apt install libasound2-dev)")
  endif()
endif()
if(NYXMAP_MIDI_API)
  # RtMidi declares its CoreMIDI client getters throw() (noexcept), yet reports a failed MIDIClientCreate by throwing
  # RtMidiError from them: std::terminate, the process aborts before Midi.cpp's catch. MIDIClientCreate does fail now
  # and then when several processes reach the MIDI server at once (the UI tests under ctest -j). Built from a copy
  # without those throw(), the error is caught and the port list is empty.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${rtmidi_SOURCE_DIR}/RtMidi.cpp")
  file(READ "${rtmidi_SOURCE_DIR}/RtMidi.cpp" rtmidi_code)
  string(REPLACE "getCoreMidiClientSingleton(const std::string& clientName) throw()"
                 "getCoreMidiClientSingleton(const std::string& clientName)" rtmidi_code "${rtmidi_code}")
  set(rtmidi_patched "${CMAKE_BINARY_DIR}/generated/rtmidi/RtMidi.cpp")
  set(rtmidi_previous "")
  if(EXISTS "${rtmidi_patched}")
    file(READ "${rtmidi_patched}" rtmidi_previous)
  endif()
  if(NOT rtmidi_previous STREQUAL rtmidi_code)
    file(WRITE "${rtmidi_patched}" "${rtmidi_code}")
  endif()
  add_library(rtmidi_static STATIC "${rtmidi_patched}")
  target_include_directories(rtmidi_static PUBLIC "${rtmidi_SOURCE_DIR}")
  target_compile_definitions(rtmidi_static PUBLIC ${NYXMAP_MIDI_API})
  target_link_libraries(rtmidi_static PUBLIC ${NYXMAP_MIDI_LIBS})
  if(MSVC)
    target_compile_options(rtmidi_static PRIVATE /W0)
  else()
    target_compile_options(rtmidi_static PRIVATE -w)
  endif()
  target_link_libraries(nyxmap_engine PRIVATE rtmidi_static)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_MIDI)
endif()

# ---------------------------------------------------------------------------
# HAP: texture blocks decoded on the CPU when the GPU cannot sample them (BC7 and BC6 on macOS).
# bcdec: single header, MIT or Unlicense, compiled in src/engine/Hap.cpp.
# ---------------------------------------------------------------------------
nyxmap_submodule(bcdec bcdec.h)
target_include_directories(nyxmap_engine PRIVATE "${CMAKE_SOURCE_DIR}/third_party/bcdec")

# ---------------------------------------------------------------------------
# Output publishing
#  - NDI and OMT: loaded at runtime (no build-time dependency)
#  - Syphon (macOS) and Spout (Windows): sources downloaded and built with NyxMap
# ---------------------------------------------------------------------------
option(NYXMAP_SYPHON "Syphon publishing (macOS)" ON)
option(NYXMAP_SPOUT "Spout publishing and inputs (Windows)" ON)

# macOS: the output windows shown through Metal
if(APPLE)
  enable_language(OBJCXX)
  target_sources(nyxmap_engine PRIVATE src/engine/Permissions.mm
    src/engine/RhiVideoSurfaceMac.mm)
  set_source_files_properties(src/engine/RhiVideoSurfaceMac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(nyxmap_engine PRIVATE "-framework Metal" "-framework QuartzCore" "-framework IOSurface"
    "-framework AppKit" "-framework Network" "-framework CoreVideo" "-framework CoreGraphics" "-framework IOKit")
elseif(WIN32)
  target_sources(nyxmap_engine PRIVATE src/engine/RhiVideoSurfaceD3D11.cpp)
  target_link_libraries(nyxmap_engine PRIVATE d3d11)
else()
  target_sources(nyxmap_engine PRIVATE src/engine/RhiVideoSurfaceNone.cpp)
endif()

if(APPLE AND NYXMAP_SYPHON)
  enable_language(C OBJC OBJCXX) # Syphon: C and Objective-C sources
  nyxmap_submodule(syphon SyphonServer.h)
  set(syphon_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/syphon")
  # The sources import <Syphon/...>: expose the headers under this prefix.
  file(GLOB SYPHON_HEADERS "${syphon_SOURCE_DIR}/*.h")
  file(COPY ${SYPHON_HEADERS} DESTINATION "${CMAKE_BINARY_DIR}/syphon-include/Syphon")
  file(GLOB SYPHON_SOURCES "${syphon_SOURCE_DIR}/*.m" "${syphon_SOURCE_DIR}/*.c")
  # The base server (an IOSurface), the Metal client (an IOSurface as a texture) and the server directory: neither the
  # OpenGL servers and clients nor the Metal server (it would require compiling .metal shaders)
  list(FILTER SYPHON_SOURCES EXCLUDE REGEX "SyphonMetalServer|OpenGL|GLShader|GLVertices|RendererGL|RendererCoreGL|RendererLegacyGL|IOSurfaceImageCore|IOSurfaceImageLegacy|SyphonServer\\.m|SyphonClient\\.m|SyphonImage\\.m")
  add_library(syphon STATIC ${SYPHON_SOURCES})
  target_include_directories(syphon PUBLIC "${CMAKE_BINARY_DIR}/syphon-include" PRIVATE "${syphon_SOURCE_DIR}")
  target_compile_definitions(syphon PRIVATE SYPHON_CORE_SHARE)
  target_compile_options(syphon PRIVATE -w "SHELL:-include ${syphon_SOURCE_DIR}/Syphon_Prefix.pch"
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
  target_link_libraries(syphon PUBLIC "-framework Cocoa" "-framework IOSurface" "-framework Foundation" "-framework Metal")
  target_sources(nyxmap_engine PRIVATE src/engine/PublishSyphon.mm src/engine/LiveInputSyphon.mm)
  set_source_files_properties(src/engine/PublishSyphon.mm src/engine/LiveInputSyphon.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(nyxmap_engine PRIVATE syphon)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_SYPHON)
endif()

# DeckLink (Blackmagic SDI / HDMI cards): the SDK's headers are in third_party/decklink (Boost license), the driver
# (Desktop Video) is loaded at run time. Windows: its interfaces come from the SDK's IDL, compiled by MIDL.
option(NYXMAP_DECKLINK "DeckLink outputs (Blackmagic Desktop Video)" ON)
if(NYXMAP_DECKLINK)
  set(DECKLINK_SDK "${CMAKE_SOURCE_DIR}/third_party/decklink")
  if(APPLE)
    target_include_directories(nyxmap_engine PRIVATE "${DECKLINK_SDK}/mac")
    target_sources(nyxmap_engine PRIVATE "${DECKLINK_SDK}/mac/DeckLinkAPIDispatch.cpp")
    set_source_files_properties("${DECKLINK_SDK}/mac/DeckLinkAPIDispatch.cpp" PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(nyxmap_engine PRIVATE "-framework CoreFoundation")
    target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_DECKLINK)
  elseif(WIN32)
    find_program(MIDL_EXE midl)
    if(MIDL_EXE)
      set(DECKLINK_GEN "${CMAKE_BINARY_DIR}/decklink")
      file(MAKE_DIRECTORY "${DECKLINK_GEN}")
      file(GLOB DECKLINK_IDL "${DECKLINK_SDK}/win/*.idl")
      add_custom_command(
        OUTPUT "${DECKLINK_GEN}/DeckLinkAPI_h.h" "${DECKLINK_GEN}/DeckLinkAPI_i.c"
        COMMAND "${MIDL_EXE}" /nologo /env x64 /I "${DECKLINK_SDK}/win" /out "${DECKLINK_GEN}"
                /h DeckLinkAPI_h.h /iid DeckLinkAPI_i.c /tlb DeckLinkAPI.tlb /proxy DeckLinkAPI_p.c /dlldata dlldata.c
                "${DECKLINK_SDK}/win/DeckLinkAPI.idl"
        DEPENDS ${DECKLINK_IDL}
        COMMENT "DeckLink SDK interfaces (MIDL)")
      add_custom_target(decklink_idl DEPENDS "${DECKLINK_GEN}/DeckLinkAPI_h.h" "${DECKLINK_GEN}/DeckLinkAPI_i.c")
      add_dependencies(nyxmap_engine decklink_idl)
      target_include_directories(nyxmap_engine PRIVATE "${DECKLINK_GEN}")
      target_link_libraries(nyxmap_engine PRIVATE ole32 oleaut32)
      target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_DECKLINK)
    else()
      message(WARNING "MIDL not found (run from a Visual Studio developer prompt): built without DeckLink outputs")
    endif()
  else()
    target_include_directories(nyxmap_engine PRIVATE "${DECKLINK_SDK}/linux")
    target_sources(nyxmap_engine PRIVATE "${DECKLINK_SDK}/linux/DeckLinkAPIDispatch.cpp")
    set_source_files_properties("${DECKLINK_SDK}/linux/DeckLinkAPIDispatch.cpp" PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(nyxmap_engine PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_DECKLINK)
  endif()
endif()

if(WIN32 AND NYXMAP_SPOUT)
  nyxmap_submodule(spout2 SPOUTSDK/SpoutGL/SpoutCopy.cpp)
  set(SPOUT_DIR "${CMAKE_SOURCE_DIR}/third_party/spout2/SPOUTSDK/SpoutGL")
  # SpoutDX only (Direct3D 11, on QRhi's device): the SDK's OpenGL interop is not built
  add_library(spout_gl STATIC
    ${SPOUT_DIR}/SpoutCopy.cpp ${SPOUT_DIR}/SpoutDirectX.cpp ${SPOUT_DIR}/SpoutFrameCount.cpp
    ${SPOUT_DIR}/SpoutSenderNames.cpp ${SPOUT_DIR}/SpoutSharedMemory.cpp ${SPOUT_DIR}/SpoutUtils.cpp
    ${CMAKE_SOURCE_DIR}/third_party/spout2/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.cpp)
  target_include_directories(spout_gl PUBLIC ${SPOUT_DIR} ${CMAKE_SOURCE_DIR}/third_party/spout2/SPOUTSDK/SpoutDirectX/SpoutDX)
  target_compile_definitions(spout_gl PUBLIC SPOUT_BUILD_STATIC)
  if(MSVC)
    target_compile_options(spout_gl PRIVATE /W0)
  endif()
  target_link_libraries(spout_gl PUBLIC d3d9 d3d11 dxgi version winmm shlwapi)
  target_sources(nyxmap_engine PRIVATE src/engine/PublishSpout.cpp src/engine/LiveInputSpout.cpp)
  target_link_libraries(nyxmap_engine PRIVATE spout_gl)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_SPOUT)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
include(cmake/TouchEngine.cmake)

# The interface, shared by the application and its UI tests: compiled once (nyxmap_ui), not once per test
set(UI_SOURCES
  src/ui/MainWindow.cpp
  src/ui/LogPanel.cpp
  src/ui/LogModel.cpp
  src/ui/AppConsole.cpp
  src/ui/StatusLine.cpp
  src/ui/ConsolePanel.cpp
  src/ui/ControlsPanel.cpp
  src/ui/ShowControlPanel.cpp
  src/ui/FxChainPanel.cpp
  src/ui/LayerInspector.cpp
  src/ui/ParamPanel.cpp
  src/ui/PresetEditor.cpp
  src/ui/MappingView.cpp src/ui/MappingViewLines.cpp src/ui/MaskEditor.cpp
  src/ui/PreviewWorkspace.cpp
  src/ui/Scene3DView.cpp
  src/ui/FixtureOverlay.cpp
  src/ui/FixturePanel.cpp
  src/ui/FixtureInspector.cpp
  src/ui/DmxOutputsPanel.cpp
  src/ui/MaskPanel.cpp
  src/ui/OutputWindow.cpp
  src/ui/RhiDraw.cpp
  src/ui/Commands.cpp
  src/ui/LayerTable.cpp
  src/ui/MediaBin.cpp
  src/ui/CompositionPanel.cpp
  src/ui/ViewportOutput.cpp
  src/ui/SettingsPanel.cpp
  src/ui/SnapshotPanel.cpp
  src/ui/SequencePanel.cpp
  src/ui/TimelineActivity.cpp
  src/ui/AnimEditor.cpp src/ui/ShowMenus.cpp
  src/ui/ParamAnimPanel.cpp
  src/ui/TimelinePanel.cpp
  src/ui/Widgets.cpp
  src/ui/ColorPicker.cpp
  src/ui/ColorPicker.h
  src/ui/InteractionPanel.cpp
  src/ui/AudioSpectrum.cpp
)
if(APPLE)
  enable_language(OBJCXX)
  list(APPEND UI_SOURCES src/ui/MacPresentation.mm)
endif()
add_library(nyxmap_ui STATIC ${UI_SOURCES})
# The interface's shaders (RhiDraw), baked for every backend
# (no GLSL: Metal, Direct3D and Vulkan)
qt_add_shaders(nyxmap_ui "nyxmap_ui_shaders"
  PREFIX "/shaders"
  BASE src/ui/shaders
  FILES src/ui/shaders/uidraw.vert src/ui/shaders/uidraw_tex.frag src/ui/shaders/uidraw_color.frag)
target_include_directories(nyxmap_ui PUBLIC src src/ui PRIVATE "${CMAKE_BINARY_DIR}/generated") # NyxMapVersion.h
target_link_libraries(nyxmap_ui PUBLIC nyxmap_engine Qt6::Widgets pxs::theme)
target_compile_options(nyxmap_ui PRIVATE ${NYXMAP_WARNINGS})
if(APPLE)
  target_link_libraries(nyxmap_ui PUBLIC "-framework AppKit")
endif()

# WebPageQt.cpp stays out of nyxmap_ui: it is compiled with or without Qt WebEngine (NYXMAP_HAS_WEB) per program
set(APP_SOURCES
  assets/branding.qrc
  src/main.cpp
  src/ui/WebPageQt.cpp
)

if(APPLE)
  set_source_files_properties(assets/NyxMap.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  add_executable(NyxMap MACOSX_BUNDLE ${APP_SOURCES} assets/NyxMap.icns)
  set_target_properties(NyxMap PROPERTIES MACOSX_BUNDLE_ICON_FILE NyxMap.icns)
  # The camera permission plugin (static, from Qt): without it Qt refuses the camera before macOS is asked
  find_package(Qt6QDarwinCameraPermissionPlugin QUIET)
  if(TARGET Qt6::QDarwinCameraPermissionPlugin)
    target_link_libraries(NyxMap PRIVATE Qt6::QDarwinCameraPermissionPlugin Qt6::QDarwinCameraPermissionPlugin_init)
    # Qt's permission handler is an Objective-C class in that static library: -ObjC keeps it in the link
    target_link_options(NyxMap PRIVATE "-ObjC")
  else()
    message(WARNING "Qt camera permission plugin not found: the camera will not be asked for on macOS")
  endif()
  # The microphone permission plugin, the same way
  find_package(Qt6QDarwinMicrophonePermissionPlugin QUIET)
  if(TARGET Qt6::QDarwinMicrophonePermissionPlugin)
    target_link_libraries(NyxMap PRIVATE Qt6::QDarwinMicrophonePermissionPlugin Qt6::QDarwinMicrophonePermissionPlugin_init)
  else()
    message(WARNING "Qt microphone permission plugin not found: the microphone will not be asked for on macOS")
  endif()
  set_target_properties(NyxMap PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "${NYXMAP_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "fr.nyxmap.app"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_LONG_VERSION_STRING "${NYXMAP_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in") # with the camera's usage text
  set(ISF_DEST "$<TARGET_FILE_DIR:NyxMap>/../Resources/isf")
  set(AUDIO_DEST "$<TARGET_FILE_DIR:NyxMap>/../Resources/audio")
elseif(WIN32)
  add_executable(NyxMap WIN32 ${APP_SOURCES} assets/NyxMap.rc)
  target_include_directories(NyxMap PRIVATE "${CMAKE_SOURCE_DIR}/assets")
  set(ISF_DEST "$<TARGET_FILE_DIR:NyxMap>/isf")
  set(AUDIO_DEST "$<TARGET_FILE_DIR:NyxMap>/audio")
else()
  add_executable(NyxMap ${APP_SOURCES})
  set(ISF_DEST "$<TARGET_FILE_DIR:NyxMap>/isf")
  set(AUDIO_DEST "$<TARGET_FILE_DIR:NyxMap>/audio")
endif()

# The source revision, for endurance-session comparison: regenerated at every build (cmake/BuildSha.cmake), not
# frozen at configure time
set(NYXMAP_BUILD_SHA_HEADER "${CMAKE_BINARY_DIR}/generated/nyxmap_build_sha.h")
add_custom_target(nyxmap_build_sha
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${NYXMAP_BUILD_SHA_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/BuildSha.cmake
  BYPRODUCTS ${NYXMAP_BUILD_SHA_HEADER}
  VERBATIM)
add_dependencies(NyxMap nyxmap_build_sha)
target_include_directories(NyxMap PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_include_directories(NyxMap PRIVATE src src/ui)
target_link_libraries(NyxMap PRIVATE nyxmap_ui)

# Web pages (the Web Page generator, src/ui/WebPageQt.cpp): Qt WebEngine when it is there, in the application only
# (nyxmap_engine has no browser: the application gives it one at startup). Without it the generator is not offered.
option(NYXMAP_WEB "Web Page generator (Qt WebEngine, when found)" ON)
set(NYXMAP_WEB_LIBS)
if(NYXMAP_WEB)
  # Homebrew: the qt formula and qtwebengine can be at different versions, and Qt's prefix then lacks WebEngine's
  # headers (its packages point there all the same); they are taken from qtwebengine's own prefix then
  get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
  get_filename_component(_qt_web "${_qt_prefix}/../qtwebengine" ABSOLUTE)
  if(NOT EXISTS "${_qt_prefix}/include/QtWebEngineWidgets" AND EXISTS "${_qt_web}/include/QtWebEngineWidgets")
    foreach(_m WebEngineWidgets WebEngineCore WebEngineCoreTools)
      set(Qt6${_m}_DIR "${_qt_web}/lib/cmake/Qt6${_m}" CACHE PATH "" FORCE)
    endforeach()
  endif()
  find_package(Qt6 QUIET COMPONENTS WebEngineWidgets Quick QuickWidgets)
endif()
if(NYXMAP_WEB AND Qt6WebEngineWidgets_FOUND AND Qt6QuickWidgets_FOUND)
  set(NYXMAP_WEB_LIBS Qt6::WebEngineWidgets Qt6::Quick Qt6::QuickWidgets)
  target_link_libraries(NyxMap PRIVATE ${NYXMAP_WEB_LIBS})
  target_compile_definitions(NyxMap PRIVATE NYXMAP_HAS_WEB)
  message(STATUS "Qt WebEngine found: Web Page generator")
else()
  message(STATUS "Qt WebEngine not found: no Web Page generator")
endif()

# Copy the bundled ISF shaders next to the executable (or into the macOS bundle).
# The folder is emptied first so renamed or deleted shaders do not linger in the build.
if(APPLE)
  # Remove legacy copies from incremental bundles: codesign treats .dsp files
  # under Contents/MacOS as nested code rather than data.
  add_custom_command(TARGET NyxMap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:NyxMap>/audio")
endif()
add_custom_command(TARGET NyxMap POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${ISF_DEST}" "${AUDIO_DEST}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/isf" "${ISF_DEST}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/audio" "${AUDIO_DEST}")

# Versioned public program/bundle name; the target and settings identity stay stable.
set_target_properties(NyxMap PROPERTIES OUTPUT_NAME "${NYXMAP_APP_NAME}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/application.json" CONTENT
  "{\"name\":\"${NYXMAP_APP_NAME}\",\"version\":\"${NYXMAP_VERSION}\",\"executable\":\"$<TARGET_FILE_NAME:NyxMap>\"}\n")

# Windows: the FFmpeg DLLs must be next to NyxMap.exe
if(WIN32 AND FFMPEG_ROOT)
  file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
  if(FFMPEG_DLLS)
    add_custom_command(TARGET NyxMap POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} "$<TARGET_FILE_DIR:NyxMap>")
  endif()
endif()

# NDI and OMT next to the program, as in the packages: NyxMap looks there first (Publish.cpp). Built once into
# build/runtimes: OMT from source (scripts/build_omt.py: git, the .NET SDK 8 or later, clang++), the NDI runtime from the
# NDI SDK (scripts/fetch_ndi.py, which accepts the NDI SDK licence agreement on your behalf: only with
# NYXMAP_ACCEPT_NDI_SDK_LICENSE). Off on CI, whose packaging steps do it themselves.
if(DEFINED ENV{CI})
  set(_runtimes_default OFF)
  set(_ndi_licence_default ON)
else()
  set(_runtimes_default ON)
  set(_ndi_licence_default OFF)
endif()
option(NYXMAP_BUNDLE_RUNTIMES "Put the NDI runtime and OMT next to the program (downloads; OMT needs git, clang++ and the .NET SDK)"
       ${_runtimes_default})
option(NYXMAP_ACCEPT_NDI_SDK_LICENSE
       "Accept the NDI SDK License Agreement (https://ndi.video/sdk/), so that the build downloads the NDI runtime"
       ${_ndi_licence_default})
if(NYXMAP_BUNDLE_RUNTIMES)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(RUNTIMES "${CMAKE_BINARY_DIR}/runtimes")
  if(APPLE)
    set(RUNTIME_DEST "$<TARGET_FILE_DIR:NyxMap>/../Frameworks")
    set(NDI_FILE libndi.dylib)
    set(OMT_FILES libomt.dylib libvmx.dylib)
  elseif(WIN32)
    set(RUNTIME_DEST "$<TARGET_FILE_DIR:NyxMap>")
    set(NDI_FILE Processing.NDI.Lib.x64.dll)
    set(OMT_FILES libomt.dll libvmx.dll)
  else()
    set(RUNTIME_DEST "$<TARGET_FILE_DIR:NyxMap>")
    set(NDI_FILE libndi.so.6)
    set(OMT_FILES libomt.so libvmx.so)
  endif()
  set(RUNTIME_FILES)
  if(NYXMAP_ACCEPT_NDI_SDK_LICENSE)
    add_custom_command(OUTPUT "${RUNTIMES}/ndi/${NDI_FILE}"
      COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/scripts/fetch_ndi.py" "${RUNTIMES}/ndi"
      COMMENT "NDI runtime from the NDI SDK" VERBATIM)
    list(APPEND RUNTIME_FILES "${RUNTIMES}/ndi/${NDI_FILE}")
  else()
    message(NOTICE "The NDI runtime is not downloaded: fetching it from the NDI SDK means accepting the NDI SDK License "
                   "Agreement (https://ndi.video/sdk/). If you accept it, configure again with "
                   "-DNYXMAP_ACCEPT_NDI_SDK_LICENSE=ON, or set an NDI library folder in NyxMap.")
  endif()
  # What scripts/build_omt.py runs: git, the .NET SDK and clang++ (Xcode's on macOS)
  find_program(DOTNET dotnet)
  find_program(NYXMAP_OMT_CLANGXX clang++)
  find_program(NYXMAP_OMT_GIT git)
  set(_omt_missing)
  foreach(_tool DOTNET NYXMAP_OMT_CLANGXX NYXMAP_OMT_GIT)
    if(NOT ${_tool})
      list(APPEND _omt_missing ${_tool})
    endif()
  endforeach()
  if(NOT _omt_missing)
    list(TRANSFORM OMT_FILES PREPEND "${RUNTIMES}/omt/" OUTPUT_VARIABLE OMT_PATHS)
    add_custom_command(OUTPUT ${OMT_PATHS}
      COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/scripts/build_omt.py" "${RUNTIMES}/omt"
      DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_omt.py"
      COMMENT "OMT (libomt, libvmx) from source" VERBATIM)
    list(APPEND RUNTIME_FILES ${OMT_PATHS})
  else()
    list(TRANSFORM _omt_missing REPLACE "^DOTNET$" "the .NET SDK, version 8 or later (https://dotnet.microsoft.com/download)")
    list(TRANSFORM _omt_missing REPLACE "^NYXMAP_OMT_CLANGXX$" "clang++")
    list(TRANSFORM _omt_missing REPLACE "^NYXMAP_OMT_GIT$" "git")
    list(JOIN _omt_missing ", " _omt_missing)
    message(WARNING "OMT is not built next to NyxMap: it needs ${_omt_missing}. "
                    "Install what is missing and configure again, or set an OMT library folder in NyxMap.")
  endif()
  if(RUNTIME_FILES)
    add_custom_target(nyxmap_runtimes DEPENDS ${RUNTIME_FILES})
    add_dependencies(NyxMap nyxmap_runtimes)
    add_custom_command(TARGET NyxMap POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DEST}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNTIME_FILES} "${RUNTIME_DEST}" VERBATIM)
  endif()
endif()

target_compile_options(nyxmap_engine PRIVATE ${NYXMAP_WARNINGS})
target_compile_options(NyxMap PRIVATE ${NYXMAP_WARNINGS})

install(TARGETS NyxMap BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Fuzzing (Clang, libFuzzer + ASan / UBSan): the HAP and Snappy readers against damaged files (test/fuzz_hap.cpp)
# ---------------------------------------------------------------------------
option(NYXMAP_FUZZ "Build the fuzz targets (requires Clang)" OFF)
if(NYXMAP_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "NYXMAP_FUZZ needs Clang (libFuzzer): -DCMAKE_CXX_COMPILER=clang++")
  endif()
  add_executable(fuzz_hap EXCLUDE_FROM_ALL test/fuzz_hap.cpp src/engine/Hap.cpp src/engine/Snappy.cpp
    src/engine/WorkerPool.cpp)
  target_include_directories(fuzz_hap PRIVATE src/engine "${CMAKE_SOURCE_DIR}/third_party/bcdec")
  target_link_libraries(fuzz_hap PRIVATE Qt6::Core)
  target_compile_options(fuzz_hap PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)
  target_link_options(fuzz_hap PRIVATE -fsanitize=fuzzer,address,undefined)
  # the codec is compiled here with the fuzzer's instrumentation, from the shared source
  add_executable(fuzz_osc EXCLUDE_FROM_ALL test/fuzz_osc.cpp third_party/pxs/src/osc/OscCodec.cpp)
  target_include_directories(fuzz_osc PRIVATE third_party/pxs/src/osc)
  target_link_libraries(fuzz_osc PRIVATE Qt6::Core)
  target_compile_options(fuzz_osc PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)
  target_link_options(fuzz_osc PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# ---------------------------------------------------------------------------
# Engine tests (headless): cmake --build build --target nyxmap_tests
# ---------------------------------------------------------------------------
option(NYXMAP_TESTS "Build the engine tests" ON)
if(NYXMAP_TESTS)
  add_executable(nyxmap_tests EXCLUDE_FROM_ALL test/engine_test.cpp test/text_writing.cpp test/operator_state.cpp test/video_test.cpp test/cue_capture.cpp test/cue_lifecycle.cpp test/show_hold.cpp test/scene3d.cpp test/path3d.cpp test/instance3d.cpp test/output_color.cpp test/golden_render.cpp test/timeline_repeat.cpp test/timeline_events.cpp test/media_playhead.cpp test/show_actions.cpp test/interaction_actions.cpp test/interaction_filters.cpp test/color_animation.cpp test/xy_controls.cpp test/show_control.cpp test/preview_sources.cpp test/recall_delay.cpp test/recall_integrity.cpp test/emergency_stop.cpp test/psd_layers.cpp test/live_inputs.cpp test/clock_generators.cpp test/file_sources.cpp test/tox.cpp src/ui/Commands.cpp src/ui/LogModel.cpp)
  target_include_directories(nyxmap_tests PRIVATE src/ui)
  target_link_libraries(nyxmap_tests PRIVATE nyxmap_engine)
  target_compile_options(nyxmap_tests PRIVATE ${NYXMAP_WARNINGS})
  target_compile_definitions(nyxmap_tests PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
  # CTest: each targeted area in a process of its own (a new engine).
  # Test media first: sh test/make_media.sh
  enable_testing()
  # Most tests are gathered into two programs, so that each is not linked against nyxmap_engine on its own:
  # nyxmap_engine_tests (the engine alone) and nyxmap_ui_tests (with nyxmap_ui). Each test file keeps its own main(),
  # renamed at compile time; CTest still runs every test in a process of its own (nyxmap_engine_tests <test> [args]).
  # nyxmap_test_main(<program> <test> <file> [WARNINGS]) adds one of them.
  function(nyxmap_test_main program test file)
    string(MAKE_C_IDENTIFIER "${test}" id)
    set_property(SOURCE ${file} APPEND PROPERTY COMPILE_DEFINITIONS main=nyxmap_test_${id})
    if(ARGN STREQUAL "WARNINGS")
      set_property(SOURCE ${file} APPEND PROPERTY COMPILE_OPTIONS ${NYXMAP_WARNINGS})
    endif()
    set_property(GLOBAL APPEND PROPERTY ${program}_SOURCES ${file})
    set_property(GLOBAL APPEND PROPERTY ${program}_TESTS ${test})
    add_test(NAME ${test} COMMAND ${program} ${test})
  endfunction()
  function(nyxmap_test_program program)
    get_property(sources GLOBAL PROPERTY ${program}_SOURCES)
    get_property(tests GLOBAL PROPERTY ${program}_TESTS)
    set(table "")
    foreach(test ${tests})
      string(MAKE_C_IDENTIFIER "${test}" id)
      string(APPEND table "int nyxmap_test_${id}(int, char **);\n")
    endforeach()
    string(APPEND table "static const TestMain kTests[] = {\n")
    foreach(test ${tests})
      string(MAKE_C_IDENTIFIER "${test}" id)
      string(APPEND table "    {\"${test}\", nyxmap_test_${id}},\n")
    endforeach()
    string(APPEND table "};\n")
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/${program}.inc" CONTENT "${table}")
    add_executable(${program} test/test_main.cpp ${sources})
    target_compile_definitions(${program} PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test"
      NYXMAP_SOURCE_DIR="${CMAKE_SOURCE_DIR}" NYXMAP_TEST_TABLE="${program}.inc")
    target_include_directories(${program} PRIVATE "${CMAKE_BINARY_DIR}/generated")
  endfunction()

  # The engine alone
  nyxmap_test_main(nyxmap_engine_tests audio_generator_engine test/audio_generator_engine.cpp)
  nyxmap_test_main(nyxmap_engine_tests remote_server test/remote_server.cpp)
  nyxmap_test_main(nyxmap_engine_tests show_controls test/show_controls.cpp)
  nyxmap_test_main(nyxmap_engine_tests beat_clock test/beat_clock.cpp)
  nyxmap_test_main(nyxmap_engine_tests audio_levels test/audio_levels.cpp)
  nyxmap_test_main(nyxmap_engine_tests show_editing test/show_editing.cpp)
  nyxmap_test_main(nyxmap_engine_tests preset_actions test/preset_actions.cpp)
  nyxmap_test_main(nyxmap_engine_tests isf_library test/isf_library.cpp)
  nyxmap_test_main(nyxmap_engine_tests pixel_mapping test/pixel_mapping.cpp WARNINGS)
  nyxmap_test_main(nyxmap_engine_tests line_generator test/line_generator.cpp WARNINGS)
  nyxmap_test_main(nyxmap_engine_tests midi_virtual test/midi_virtual.cpp)
  nyxmap_test_main(nyxmap_engine_tests show_clock test/show_clock.cpp)
  nyxmap_test_main(nyxmap_engine_tests audio_snapshot test/audio_snapshot.cpp)
  nyxmap_test_main(nyxmap_engine_tests decoder_alpha test/decoder_alpha.cpp)
  nyxmap_test_main(nyxmap_engine_tests publishing_concurrency test/publishing_concurrency.cpp)
  nyxmap_test_main(nyxmap_engine_tests interaction_graph test/interaction_graph.cpp)
  nyxmap_test_main(nyxmap_engine_tests cue_dispatch test/cue_dispatch.cpp)
  nyxmap_test_program(nyxmap_engine_tests)
  target_link_libraries(nyxmap_engine_tests PRIVATE nyxmap_engine Qt6::Widgets)

  # With the interface
  nyxmap_test_main(nyxmap_ui_tests interaction_graph_engine test/interaction_graph_engine.cpp)
  nyxmap_test_main(nyxmap_ui_tests output_color_state test/output_color_state.cpp)
  nyxmap_test_main(nyxmap_ui_tests color_timeline_ui test/color_timeline_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests timeline_events_ui test/timeline_events_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests audio_interaction_ui test/audio_interaction_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests interaction_actions_ui test/interaction_actions_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests animation_drivers test/animation_drivers.cpp)
  nyxmap_test_main(nyxmap_ui_tests interaction_filters_ui test/interaction_filters_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests snapshot_update_ui test/snapshot_update_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests preset_snapshot_integrity test/preset_snapshot_integrity.cpp)
  nyxmap_test_main(nyxmap_ui_tests timeline_activity test/timeline_activity.cpp)
  nyxmap_test_main(nyxmap_ui_tests sequence_ui test/sequence_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests number_widgets test/number_widgets.cpp)
  nyxmap_test_main(nyxmap_ui_tests mask_ui test/mask_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests line_ui test/line_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests preview_ui test/preview_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests fixture_ui test/fixture_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests show_editing_ui test/show_editing_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests recall_delay_ui test/recall_delay_ui.cpp)
  nyxmap_test_main(nyxmap_ui_tests color_picker_ui test/color_picker_ui.cpp)
  nyxmap_test_program(nyxmap_ui_tests)
  target_link_libraries(nyxmap_ui_tests PRIVATE nyxmap_ui)

  # Programs of their own: no nyxmap_engine (light), engine sources built differently, their own operator new
  # (allocation counts), or linker wrapping
  add_executable(nyxmap_audio_generators_test test/audio_generators.cpp)
  target_link_libraries(nyxmap_audio_generators_test PRIVATE nyxmap_engine)
  add_test(NAME audio_generators COMMAND nyxmap_audio_generators_test)
  add_executable(nyxmap_audio_effects_test test/audio_effects.cpp)
  target_link_libraries(nyxmap_audio_effects_test PRIVATE nyxmap_engine)
  add_test(NAME audio_effects COMMAND nyxmap_audio_effects_test)
  add_executable(nyxmap_audio_resources_test test/audio_resources.cpp)
  target_include_directories(nyxmap_audio_resources_test PRIVATE src/engine)
  target_link_libraries(nyxmap_audio_resources_test PRIVATE Qt6::Core)
  add_test(NAME audio_resources COMMAND nyxmap_audio_resources_test)
  # Test the dependency-free generated backend even on hosts with libfaust installed.
  add_executable(nyxmap_audio_generators_portable_test test/audio_generators.cpp src/engine/AudioGenerator.cpp src/engine/FaustInterpreter.cpp src/engine/AudioEffects.cpp src/engine/AudioStream.cpp src/engine/AudioCapture.cpp test/portable_audio_capture.cpp)
  target_include_directories(nyxmap_audio_generators_portable_test PRIVATE src/engine third_party/faust/architecture)
  target_link_libraries(nyxmap_audio_generators_portable_test PRIVATE Qt6::Core ffmpeg)
  add_test(NAME audio_generators_portable COMMAND nyxmap_audio_generators_portable_test)
  add_executable(nyxmap_audio_hits_test test/audio_hits.cpp)
  target_include_directories(nyxmap_audio_hits_test PRIVATE src/engine)
  add_test(NAME audio_hits COMMAND nyxmap_audio_hits_test)
  add_executable(nyxmap_image_import_test test/image_import.cpp)
  target_include_directories(nyxmap_image_import_test PRIVATE src/engine)
  target_link_libraries(nyxmap_image_import_test PRIVATE Qt6::Gui)
  add_test(NAME image_import COMMAND nyxmap_image_import_test)
  add_executable(nyxmap_benchmark_rates_test test/benchmark_rates.cpp)
  add_test(NAME benchmark_rates COMMAND nyxmap_benchmark_rates_test)
  # QRhi (docs/developers/qrhi-prototype.md): every ISF baked for Metal, Direct3D and Vulkan by QShaderBaker
  add_executable(nyxmap_isf_rhi_test test/isf_rhi.cpp)
  target_link_libraries(nyxmap_isf_rhi_test PRIVATE nyxmap_engine)
  target_compile_definitions(nyxmap_isf_rhi_test PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME isf_rhi COMMAND nyxmap_isf_rhi_test)
  # An output window shown through the QRhi renderer's swapchain: what it presents is the viewport's picture
  add_executable(nyxmap_rhi_output_test test/rhi_output.cpp)
  target_include_directories(nyxmap_rhi_output_test PRIVATE src/engine)
  target_link_libraries(nyxmap_rhi_output_test PRIVATE nyxmap_engine)
  target_compile_definitions(nyxmap_rhi_output_test PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
  add_test(NAME rhi_output COMMAND nyxmap_rhi_output_test)
  set_tests_properties(rhi_output PROPERTIES SKIP_RETURN_CODE 77)
  # The interface's previews with the QRhi renderer: its pictures read back as pixels
  add_executable(nyxmap_rhi_preview_test test/rhi_preview.cpp)
  target_include_directories(nyxmap_rhi_preview_test PRIVATE src/engine)
  target_link_libraries(nyxmap_rhi_preview_test PRIVATE nyxmap_engine)
  target_compile_definitions(nyxmap_rhi_preview_test PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
  add_test(NAME rhi_preview COMMAND nyxmap_rhi_preview_test)
  set_tests_properties(rhi_preview PROPERTIES SKIP_RETURN_CODE 77)
  # What the interface's pictures cost a frame: cmake --build build --target nyxmap_preview_cost
  add_executable(nyxmap_preview_cost EXCLUDE_FROM_ALL test/preview_cost.cpp)
  target_include_directories(nyxmap_preview_cost PRIVATE src/engine)
  target_link_libraries(nyxmap_preview_cost PRIVATE nyxmap_engine)
  # A real TouchDesigner component (NYXMAP_TOX): skipped without one
  add_executable(nyxmap_tox_real_test test/tox_real.cpp)
  target_include_directories(nyxmap_tox_real_test PRIVATE src/engine)
  target_link_libraries(nyxmap_tox_real_test PRIVATE nyxmap_engine)
  add_test(NAME tox_real COMMAND nyxmap_tox_real_test)
  set_tests_properties(tox_real PROPERTIES SKIP_RETURN_CODE 77)
  # These tests each create their own native GPU device/context (D3D11 on Windows, Metal on macOS,
  # Vulkan elsewhere). Running several at once can exhaust software/CI adapters and crash drivers;
  # serialize GPU integration tests while keeping the CPU-only suite parallel.
  set_tests_properties(isf_rhi rhi_output rhi_preview PROPERTIES RUN_SERIAL TRUE)
  add_executable(nyxmap_naming_test test/naming.cpp)
  target_include_directories(nyxmap_naming_test PRIVATE src/engine)
  target_link_libraries(nyxmap_naming_test PRIVATE Qt6::Core)
  add_test(NAME naming COMMAND nyxmap_naming_test)
  # GNU-compatible Linux linkers inject failures at our FFmpeg call sites only.
  # No test hooks or extra branches are compiled into the production decoder.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(nyxmap_video_transfer_test test/video_transfer.cpp)
    target_link_libraries(nyxmap_video_transfer_test PRIVATE nyxmap_engine)
    target_link_options(nyxmap_video_transfer_test PRIVATE -Wl,--wrap=av_hwframe_transfer_data)
    add_test(NAME video_transfer COMMAND nyxmap_video_transfer_test)
    add_executable(nyxmap_decoder_allocations_test test/decoder_allocations.cpp)
    target_link_libraries(nyxmap_decoder_allocations_test PRIVATE nyxmap_engine)
    target_compile_options(nyxmap_decoder_allocations_test PRIVATE ${NYXMAP_WARNINGS})
    target_compile_definitions(nyxmap_decoder_allocations_test PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
    target_link_options(nyxmap_decoder_allocations_test PRIVATE
      -Wl,--wrap=avcodec_alloc_context3 -Wl,--wrap=avcodec_parameters_to_context
      -Wl,--wrap=av_frame_alloc -Wl,--wrap=av_packet_alloc
      -Wl,--wrap=av_seek_frame -Wl,--wrap=avcodec_flush_buffers)
    add_test(NAME decoder_allocations COMMAND nyxmap_decoder_allocations_test)
    set_tests_properties(decoder_allocations PROPERTIES TIMEOUT 20)
  endif()
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    # Render one small frame of each example with strict layer and effect error checks.
    add_test(NAME demo_fixture COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/examples/demo/build_show.py --out ${CMAKE_BINARY_DIR}/demo-fixture)
    set_tests_properties(demo_fixture PROPERTIES FIXTURES_SETUP demo)
    add_test(NAME demo_render COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/render_example.py
      $<TARGET_FILE:NyxMap> ${CMAKE_BINARY_DIR}/demo-fixture/Demo.nyxmap ${CMAKE_BINARY_DIR}/demo-preview.png)
    set_tests_properties(demo_render PROPERTIES FIXTURES_REQUIRED demo TIMEOUT 120)
    add_test(NAME pbr_render COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/render_example.py
      $<TARGET_FILE:NyxMap> ${CMAKE_SOURCE_DIR}/examples/pbr/PBR.nyxmap ${CMAKE_BINARY_DIR}/pbr-preview.png)
    set_tests_properties(pbr_render PROPERTIES TIMEOUT 300)
    add_test(NAME endurance_analysis COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/test -p test_endurance_analysis.py)
    add_test(NAME ci_scope COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/.github/scripts -p test_ci_scope.py)
  endif()
  add_executable(nyxmap_mask_geometry_test test/mask_geometry.cpp src/engine/Masks.cpp)
  target_include_directories(nyxmap_mask_geometry_test PRIVATE src/engine)
  target_link_libraries(nyxmap_mask_geometry_test PRIVATE Qt6::Gui)
  add_test(NAME mask_geometry COMMAND nyxmap_mask_geometry_test)
  find_package(Threads REQUIRED)
  add_executable(nyxmap_realtime_timing_test test/realtime_timing.cpp)
  target_include_directories(nyxmap_realtime_timing_test PRIVATE src/engine)
  target_link_libraries(nyxmap_realtime_timing_test PRIVATE Threads::Threads)
  add_test(NAME realtime_timing COMMAND nyxmap_realtime_timing_test)
  add_executable(nyxmap_audio_analysis_test test/audio_analysis.cpp)
  target_include_directories(nyxmap_audio_analysis_test PRIVATE src/engine)
  add_test(NAME audio_analysis COMMAND nyxmap_audio_analysis_test)
  add_executable(nyxmap_audio_analysis_concurrency_test test/audio_analysis_concurrency.cpp)
  target_include_directories(nyxmap_audio_analysis_concurrency_test PRIVATE src/engine)
  target_link_libraries(nyxmap_audio_analysis_concurrency_test PRIVATE Threads::Threads)
  add_test(NAME audio_analysis_concurrency COMMAND nyxmap_audio_analysis_concurrency_test)
  # The Web Page generator: settings, parameters, persistence; a real page too when Qt WebEngine is there (it needs a
  # display: xvfb-run on Linux; Chromium's sandbox is turned off in the test only)
  add_executable(nyxmap_web_page_test test/web_page.cpp src/ui/WebPageQt.cpp)
  target_include_directories(nyxmap_web_page_test PRIVATE src/ui)
  target_link_libraries(nyxmap_web_page_test PRIVATE nyxmap_engine Qt6::Widgets ${NYXMAP_WEB_LIBS})
  target_compile_options(nyxmap_web_page_test PRIVATE ${NYXMAP_WARNINGS})
  if(NYXMAP_WEB_LIBS)
    target_compile_definitions(nyxmap_web_page_test PRIVATE NYXMAP_HAS_WEB)
  endif()
  add_test(NAME web_page COMMAND nyxmap_web_page_test)
  set_tests_properties(web_page PROPERTIES TIMEOUT 120)
  set_tests_properties(audio_generators audio_generators_portable audio_generator_engine beat_clock
    audio_levels audio_effects show_editing preset_actions pixel_mapping line_generator
    publishing_concurrency cue_dispatch animation_drivers interaction_graph_engine
    show_editing_ui audio_analysis_concurrency PROPERTIES TIMEOUT 60)
  # Start late in a busy run on a virtual GPU (the device's setup alone takes long under load): not a hang
  set_tests_properties(show_controls fixture_ui PROPERTIES TIMEOUT 120)
  set_tests_properties(remote_server audio_hits PROPERTIES TIMEOUT 120)
  set_tests_properties(color_timeline_ui timeline_events_ui midi_virtual realtime_timing timeline_activity color_picker_ui
    PROPERTIES TIMEOUT 30)
  set_tests_properties(show_clock decoder_alpha PROPERTIES TIMEOUT 15)

  # Decoder seeks are sensitive to CPU/GPU contention on Apple Silicon runners.
  # Keep these two integration tests isolated while the rest of the suite runs with ctest -j4.
  if(APPLE)
    set_tests_properties(preset_actions cue_dispatch PROPERTIES RUN_SERIAL TRUE)
  endif()

  foreach(area scene3d path3d instance3d show_control xy_controls color_animation output_color show_hold recall_integrity operator_state preview_sources emergency_stop cue_capture cue_lifecycle  golden timeline_repeat timeline_events media_playhead show_actions interaction_actions interaction_filters recall_delay isf_safe video project playback undo structure compositing osc snapshots masks rendering thread publishing
          media sound layersource timecode interactions psd live_inputs clocks text_writing file_sources tox)
    add_test(NAME ${area} COMMAND nyxmap_tests --area ${area})
  endforeach()
  # Areas depending on real time (sound clocks, threads, waits, publishers): label "timing".
  # CI gates on the first attempt; run_timing_tests.py reruns failures for diagnosis only.
  set_tests_properties(sound playback thread publishing timecode snapshots media file_sources PROPERTIES LABELS timing)
  # The whole interface in one program (also the sanitizer and Valgrind jobs' target)
  add_executable(nyxmap_workspace_ui_test test/workspace_ui.cpp test/xy_parameter_ui.cpp test/color_parameter_ui.cpp test/clock_preset_ui.cpp test/layer_source_ui.cpp test/scene3d_ui.cpp test/audio_fx_ui.cpp test/console_ui.cpp test/controls_ui.cpp test/show_control_ui.cpp assets/branding.qrc)
  target_include_directories(nyxmap_workspace_ui_test PRIVATE "${CMAKE_BINARY_DIR}/generated")
  # Without Qt WebEngine (no WebPageQt.cpp): the Media Bin then does not offer the Web Page generator
  target_link_libraries(nyxmap_workspace_ui_test PRIVATE nyxmap_ui)
  target_compile_definitions(nyxmap_workspace_ui_test PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
  add_test(NAME workspace_ui COMMAND nyxmap_workspace_ui_test)
  add_test(NAME clock_preset_ui COMMAND nyxmap_workspace_ui_test --clock-presets)
  add_test(NAME layer_source_ui COMMAND nyxmap_workspace_ui_test --layer-source)
  add_test(NAME scene3d_ui COMMAND nyxmap_workspace_ui_test --scene3d)
  add_test(NAME controls_ui COMMAND nyxmap_workspace_ui_test --controls)
  add_test(NAME show_control_ui COMMAND nyxmap_workspace_ui_test --show-control)
  add_test(NAME audio_fx_ui COMMAND nyxmap_workspace_ui_test --audio-fx)
  add_test(NAME console_ui COMMAND nyxmap_workspace_ui_test --console)
  add_test(NAME system_isf_ui COMMAND nyxmap_workspace_ui_test --system-isf)
  add_test(NAME xy_parameter_ui COMMAND nyxmap_workspace_ui_test --xy)
  add_test(NAME color_parameter_ui COMMAND nyxmap_workspace_ui_test --color-animation)
  add_test(NAME selection_ui COMMAND nyxmap_workspace_ui_test --selection) # not in the Valgrind job: see the test
  set_tests_properties(show_control_ui controls_ui audio_fx_ui console_ui clock_preset_ui xy_parameter_ui color_parameter_ui
    PROPERTIES TIMEOUT 60)
  # The Apple software renderer takes longer when the UI suite is under load.
  set_tests_properties(clock_preset_ui PROPERTIES TIMEOUT 120)
  if(NOT WIN32) # the application's exit codes in --render mode (Windows: its Qt libraries are deployed separately)
    add_test(NAME render_cli COMMAND ${CMAKE_COMMAND} -DNYXMAP=$<TARGET_FILE:NyxMap> -DOUT=${CMAKE_BINARY_DIR}/render_cli
             -P "${CMAKE_SOURCE_DIR}/test/render_cli.cmake")
  endif()
  # The tests that show real windows share the one desktop. Side by side (ctest -j), one takes the key window from
  # another, whose menus and dialogs macOS then closes as they open: a check finds no menu, or Qt's Cocoa plugin frees
  # a menu's window inside its own show and the test crashes. They take turns; the headless tests still run beside them.
  get_property(nyxmap_all_tests DIRECTORY PROPERTY TESTS)
  set(nyxmap_window_tests "")
  foreach(test ${nyxmap_all_tests})
    if(test MATCHES "_ui$" OR test MATCHES "^(rendering|animation_drivers|timeline_activity|number_widgets)$")
      list(APPEND nyxmap_window_tests ${test})
    endif()
  endforeach()
  set_tests_properties(${nyxmap_window_tests} PROPERTIES RESOURCE_LOCK desktop COST 50)
  # The longest first (CTest starts the highest cost first; a CI runner has no timings of a previous run): the window
  # tests' queue, then what a software renderer (macOS runner) takes minutes over, the rest beside them
  set_tests_properties(scene3d_ui preview_ui PROPERTIES COST 100)
  set_tests_properties(isf_safe scene3d instance3d PROPERTIES COST 90)
  set_tests_properties(project output_color video masks PROPERTIES COST 40)
  # Several videos played together: cmake --build build --target nyxmap_bench
  # What the interface's 3D view costs: cmake --build build --target nyxmap_scene_view_bench
  add_executable(nyxmap_scene_view_bench EXCLUDE_FROM_ALL test/scene_view_bench.cpp)
  target_link_libraries(nyxmap_scene_view_bench PRIVATE nyxmap_engine Qt6::Widgets)
  add_executable(nyxmap_isf_bench EXCLUDE_FROM_ALL test/isf_bench.cpp)
  target_link_libraries(nyxmap_isf_bench PRIVATE nyxmap_engine Qt6::Widgets)
  add_executable(nyxmap_feedback_bench EXCLUDE_FROM_ALL test/feedback_bench.cpp)
  target_link_libraries(nyxmap_feedback_bench PRIVATE nyxmap_engine Qt6::Widgets)
  add_executable(nyxmap_video_latency_bench EXCLUDE_FROM_ALL test/video_latency_bench.cpp)
  target_link_libraries(nyxmap_video_latency_bench PRIVATE nyxmap_engine Qt6::Widgets)
  add_executable(nyxmap_bench EXCLUDE_FROM_ALL test/bench.cpp)
  target_link_libraries(nyxmap_bench PRIVATE nyxmap_engine)
endif()

# TouchEngine is a redistributable SDK shim, not a TouchDesigner installation.
if(NYXMAP_TOUCHENGINE_FRAMEWORK)
  set_property(TARGET NyxMap APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
  add_custom_command(TARGET NyxMap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:NyxMap>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND}
      "-DSOURCE=${NYXMAP_TOUCHENGINE_FRAMEWORK}"
      "-DDESTINATION=$<TARGET_BUNDLE_DIR:NyxMap>/Contents/Frameworks/TouchEngine.framework"
      -P "${NYXMAP_TOUCHENGINE_STAGE_SCRIPT}"
    VERBATIM)
elseif(NYXMAP_TOUCHENGINE_DLL)
  add_custom_command(TARGET NyxMap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NYXMAP_TOUCHENGINE_DLL}" "$<TARGET_FILE_DIR:NyxMap>/TouchEngine.dll")
endif()
