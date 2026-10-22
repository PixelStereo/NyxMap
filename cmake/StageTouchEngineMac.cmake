# Run only on macOS. The SDK contains unsigned nested code; signing just the
# outer framework leaves dyld unable to load it on Apple Silicon.
if(NOT IS_ABSOLUTE "${SOURCE}" OR NOT IS_ABSOLUTE "${DESTINATION}" OR
   NOT DESTINATION MATCHES "/TouchEngine\\.framework$" OR SOURCE STREQUAL DESTINATION)
  message(FATAL_ERROR "Expected distinct absolute source and destination TouchEngine framework paths")
endif()

function(checked_execute)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result ERROR_VARIABLE error)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "TouchEngine staging failed (${result}): ${ARGV}\n${error}")
  endif()
endfunction()

# Preserve versioned framework symlinks and remove stale SDK/signature files.
file(REMOVE_RECURSE "${DESTINATION}")
checked_execute(/usr/bin/ditto "${SOURCE}" "${DESTINATION}")
set(nested "${DESTINATION}/Versions/A/Frameworks")
# Sign inside out, including the helper executable. Do not use --deep to sign.
# These are the components in the pinned SDK (see Derivative's signing guide).
foreach(component IN ITEMS
    "${nested}/IPM.framework/Versions/A/ca.derivative.IPMAgent"
    "${nested}/IPM.framework"
    "${nested}/libTPC.dylib"
    "${nested}/libTouchEngine.dylib"
    "${DESTINATION}")
  checked_execute(/usr/bin/codesign --force --sign - "${component}")
endforeach()
checked_execute(/usr/bin/codesign --verify --deep --strict "${DESTINATION}")
