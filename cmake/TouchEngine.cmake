# The SDK download is opt-in. Acceptance concerns the builder's own TouchDesigner
# licence; Pinol does not activate or accept an end-user licence on their behalf.
option(PINOL_ACCEPT_TOUCHENGINE_LICENSE
  "I have accepted the TouchDesigner licence and the TouchEngine Shared Use License (see licenses/touchengine.txt); download and include the TouchEngine SDK" OFF)
if(PINOL_ACCEPT_TOUCHENGINE_LICENSE AND (APPLE OR WIN32))
  include(FetchContent)
  if(APPLE)
    set(PINOL_TOUCHENGINE_REV c3ceb1ab540db44dbfed4fb1bdb7bbfbb0305962)
    set(_te_platform macOS)
  else()
    set(PINOL_TOUCHENGINE_REV af3e4a393086c0377cd3634172a0ce18d1a654c8)
    set(_te_platform Windows)
  endif()
  FetchContent_Declare(pinol_touchengine
    GIT_REPOSITORY https://github.com/TouchDesigner/TouchEngine-${_te_platform}.git
    GIT_TAG ${PINOL_TOUCHENGINE_REV}
    GIT_PROGRESS FALSE
    SOURCE_SUBDIR pinol-no-subproject)
  FetchContent_MakeAvailable(pinol_touchengine)
  add_library(pinol_touchengine_sdk SHARED IMPORTED GLOBAL)
  if(APPLE)
    # Test/CLI executables also load the SDK via their build RPATH. Link against
    # a signed private copy rather than the unsigned FetchContent checkout.
    set(PINOL_TOUCHENGINE_FRAMEWORK "${CMAKE_BINARY_DIR}/runtimes/touchengine/TouchEngine.framework")
    set(PINOL_TOUCHENGINE_STAGE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/StageTouchEngineMac.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PINOL_TOUCHENGINE_STAGE_SCRIPT}")
    execute_process(COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE=${pinol_touchengine_SOURCE_DIR}/TouchEngine.framework"
      "-DDESTINATION=${PINOL_TOUCHENGINE_FRAMEWORK}"
      -P "${PINOL_TOUCHENGINE_STAGE_SCRIPT}"
      RESULT_VARIABLE _te_staging_result)
    if(NOT _te_staging_result STREQUAL "0")
      message(FATAL_ERROR "Could not prepare the signed TouchEngine SDK")
    endif()
    set_target_properties(pinol_touchengine_sdk PROPERTIES
      IMPORTED_LOCATION "${PINOL_TOUCHENGINE_FRAMEWORK}/TouchEngine"
      FRAMEWORK TRUE
      INTERFACE_INCLUDE_DIRECTORIES "${pinol_touchengine_SOURCE_DIR}")
    # Qt hides deprecated OpenGL SDK declarations; TouchEngine's headers use CGL.
    target_compile_definitions(pinol_engine PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(pinol_engine PRIVATE "-framework OpenGL")
  else()
    set(PINOL_TOUCHENGINE_DLL "${pinol_touchengine_SOURCE_DIR}/bin/TouchEngine.dll")
    set_target_properties(pinol_touchengine_sdk PROPERTIES
      IMPORTED_IMPLIB "${pinol_touchengine_SOURCE_DIR}/lib/TouchEngine.lib"
      IMPORTED_LOCATION "${PINOL_TOUCHENGINE_DLL}"
      INTERFACE_INCLUDE_DIRECTORIES "${pinol_touchengine_SOURCE_DIR}/include")
    target_link_libraries(pinol_engine PRIVATE opengl32)
    # Also used by the tests; multi-config executables are in their configuration directory.
    file(COPY "${PINOL_TOUCHENGINE_DLL}" DESTINATION "${CMAKE_BINARY_DIR}")
    foreach(_te_config Debug Release RelWithDebInfo MinSizeRel)
      file(COPY "${PINOL_TOUCHENGINE_DLL}" DESTINATION "${CMAKE_BINARY_DIR}/${_te_config}")
    endforeach()
  endif()
  target_compile_definitions(pinol_engine PRIVATE PINOL_HAS_TOUCHENGINE)
  target_link_libraries(pinol_engine PRIVATE pinol_touchengine_sdk)
  message(STATUS "TouchEngine SDK ${_te_platform} ${PINOL_TOUCHENGINE_REV}: licence acceptance enabled")
else()
  message(STATUS "TouchEngine SDK disabled (Windows/macOS and explicit PINOL_ACCEPT_TOUCHENGINE_LICENSE=ON required)")
endif()
