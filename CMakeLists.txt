cmake_minimum_required(VERSION 3.26)
project(MHY_Scanner
	VERSION 1.2.0
	LANGUAGES CXX
)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UNIT_TESTS "Unit Tests" OFF)
option(DEV "DEV" OFF)

add_definitions(-DMHY_Scanner_VERSION="${PROJECT_VERSION}")

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	add_compile_options(/Zi /sdl /arch:AVX2 /utf-8)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		add_compile_options(/O2)
		add_link_options(/DEBUG /OPT:REF,ICF,LBR)
	endif()
endif()

include(FetchContent)
include(ExternalProject)
include(cmake/install_ffmpeg.cmake)
include(cmake/build_QR-Code-generator.cmake)

FetchContent_Declare(
	nlohmann_json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

# MHY_Scanner 1.2 switched the hot QR decode path from OpenCV WeChatQRCode
# models to zxing-cpp. Pin the last 2.x release because its public API still
# exposes ZXing::Result/Barcode behavior seen in the 1.2 PDB.
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES_QT OFF CACHE BOOL "" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	zxing_cpp
	GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
	GIT_TAG v2.3.0
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zxing_cpp)

find_package(unofficial-webview2 CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)

find_package(OpenCV REQUIRED opencv_world)
include_directories(${OpenCV_INCLUDE_DIRS})

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/3rdparty/boost)

if(NOT DEV)
	add_definitions(-DTESTSPEED)
	add_definitions(-DSHOW)
endif()

include_directories(src/Core)

# The closed 1.2 binary requests Bilibili qn=80. The last public source still
# contains qn=10000 in a file with a very large generated Douyin cookie. Build
# from a generated copy so the reconstructed branch exactly uses qn=80 without
# touching unrelated Douyin behavior.
set(LOWLATENCY_GENERATED_DIR "${CMAKE_BINARY_DIR}/lowlatency-generated")
file(MAKE_DIRECTORY "${LOWLATENCY_GENERATED_DIR}")
file(READ "${CMAKE_SOURCE_DIR}/src/Core/LiveStreamLink.cpp" LIVE_STREAM_LINK_SOURCE)
string(FIND "${LIVE_STREAM_LINK_SOURCE}" "{ \"qn\", \"10000\" }" LIVE_STREAM_QN_POS)
if(LIVE_STREAM_QN_POS EQUAL -1)
	message(FATAL_ERROR "Expected Bilibili qn=10000 source pattern was not found")
endif()
string(REPLACE "{ \"qn\", \"10000\" }" "{ \"qn\", \"80\" }" LIVE_STREAM_LINK_SOURCE "${LIVE_STREAM_LINK_SOURCE}")
set(LOWLATENCY_LIVE_STREAM_CPP "${LOWLATENCY_GENERATED_DIR}/LiveStreamLink.cpp")
file(WRITE "${LOWLATENCY_LIVE_STREAM_CPP}" "${LIVE_STREAM_LINK_SOURCE}")

add_executable(${PROJECT_NAME}
	"${CMAKE_SOURCE_DIR}/src/Resources/MHY_Scanner.rc"
	"${CMAKE_SOURCE_DIR}/src/Resources/MHY_Scanner.qrc"
	"${CMAKE_SOURCE_DIR}/src/UI/Main.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForStream.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForStream.h"
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForScreen.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/QRCodeForScreen.h"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowAbout.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowAbout.h"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowMain.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowMain.h"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowLogin.h"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowLogin.cpp"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowGeeTest.h"
	"${CMAKE_SOURCE_DIR}/src/UI/WindowGeeTest.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/CryptoKit.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/CryptoKit.h"
	"${LOWLATENCY_LIVE_STREAM_CPP}"
	"${CMAKE_SOURCE_DIR}/src/Core/LiveStreamLink.h"
	"${CMAKE_SOURCE_DIR}/src/Core/QRScanner.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/QRScanner.h"
	"${CMAKE_SOURCE_DIR}/src/Core/LowLatencyHttp.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ScreenScan.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ScreenScan.h"
	"${CMAKE_SOURCE_DIR}/src/Core/ConfigDate.cpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ConfigDate.h"
	"${CMAKE_SOURCE_DIR}/src/Core/BSGameSDK.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/CookieParser.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/CreateUUID.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ScannerBase.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/compile_string.hpp"
	"${CMAKE_SOURCE_DIR}/src/Core/ApiDefs.hpp"
)

if(NOT DEV)
	set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set(PROJECT_LIBRARIES
	Ws2_32
	wldap32
	Normaliz
	Dbghelp
	Qt6::Core
	Qt6::Widgets
	cpr::cpr
	QR_Code_generator
	ZXing::Core
	unofficial::webview2::webview2
	nlohmann_json::nlohmann_json
	${OpenCV_LIBS}
	${FFMPEG_LIBS}
	OpenSSL::Crypto
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_LIBRARIES})

file(GLOB FFMPEG_DLL_FILES
	"${FFMPEG_BIN_DIR}/avcodec-*.dll"
	"${FFMPEG_BIN_DIR}/avformat-*.dll"
	"${FFMPEG_BIN_DIR}/avutil-*.dll"
	"${FFMPEG_BIN_DIR}/swscale-*.dll"
	"${FFMPEG_BIN_DIR}/swresample-*.dll"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	${FFMPEG_DLL_FILES}
	"$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

if(UNIT_TESTS)
	enable_testing()
	add_subdirectory(tests)
	target_link_libraries(test_core PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_gui PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_video PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_video_hw PRIVATE ${PROJECT_LIBRARIES})
	target_link_libraries(test_streamlink PRIVATE ${PROJECT_LIBRARIES})
endif()

set(Install_Directory "${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}_${PROJECT_VERSION}")

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${Install_Directory})
install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION ${Install_Directory})

set(QT_DLL_FILES
	${QT6_INSTALL_PREFIX}/bin/Qt6Core.dll
	${QT6_INSTALL_PREFIX}/bin/Qt6Widgets.dll
	${QT6_INSTALL_PREFIX}/bin/Qt6Gui.dll
)

install(
	FILES
	${QT_DLL_FILES}
	${FFMPEG_DLL_FILES}
	DESTINATION
	${Install_Directory}
)

install(
	FILES
	${QT6_INSTALL_PREFIX}/plugins/platforms/qdirect2d.dll
	${QT6_INSTALL_PREFIX}/plugins/platforms/qminimal.dll
	${QT6_INSTALL_PREFIX}/plugins/platforms/qoffscreen.dll
	${QT6_INSTALL_PREFIX}/plugins/platforms/qwindows.dll
	DESTINATION
	${Install_Directory}/plugins/platforms
)

install(
	FILES
	${QT6_INSTALL_PREFIX}/plugins/styles/qmodernwindowsstyle.dll
	DESTINATION
	${Install_Directory}/plugins/styles
)
