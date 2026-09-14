# Last updated: 2026-09-15 00:18:00 +08:00.
# Opt in with -DCMAKE_PROJECT_MHY_Scanner_INCLUDE=<source>/tests/live_stream_probe.cmake.
function(add_local_stream_probe)
    add_executable(live_stream_probe
        "${CMAKE_SOURCE_DIR}/tests/live_stream_probe.cpp"
        "${CMAKE_SOURCE_DIR}/src/UI/QRCodeForStream.cpp"
        "${CMAKE_SOURCE_DIR}/src/UI/QRCodeForStream.h"
        "${CMAKE_SOURCE_DIR}/src/Core/QRScanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/Core/CryptoKit.cpp"
        "${CMAKE_SOURCE_DIR}/src/Core/ConfigDate.cpp"
        "${LOWLATENCY_LIVE_STREAM_CPP}")
    target_include_directories(live_stream_probe PRIVATE "${CMAKE_SOURCE_DIR}/src/UI")
    target_link_libraries(live_stream_probe PRIVATE ${PROJECT_LIBRARIES})
endfunction()
cmake_language(DEFER CALL add_local_stream_probe)
