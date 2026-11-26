cmake_minimum_required(VERSION 3.20)
project(photosphere_linux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PS_WITH_LIBRAW "Enable DNG decoding through LibRaw" ON)
option(PS_WITH_FFMPEG "Enable INSV decoding through libav" ON)
option(PS_BUILD_GUI "Build the Qt 6 GUI (phase 2)" OFF)
option(PS_BUILD_TESTS "Build unit tests" ON)

find_package(OpenCV 4 REQUIRED COMPONENTS
  core imgproc imgcodecs features2d flann photo stitching calib3d)

find_package(Threads REQUIRED)

# Core: portable geometry, stitching and metadata algorithms.
add_library(ps_core
  src/core/gpu_info.cpp
  src/core/rotation.cpp
  src/core/spherical.cpp
  src/core/lens_model.cpp
  src/core/local_warp.cpp
  src/core/exposure.cpp
  src/core/seam.cpp
  src/core/seam_carving.cpp
  src/core/blender.cpp
  src/core/pose_refiner.cpp
  src/core/stitcher.cpp
  src/core/renderer.cpp
  src/core/gpano.cpp
  src/core/little_planet.cpp)
target_include_directories(ps_core PUBLIC src)
target_link_libraries(ps_core PUBLIC ${OpenCV_LIBS} Threads::Threads)

# I/O: image readers, INSV demux, frame store.
add_library(ps_io
  src/io/exif_pose.cpp
  src/io/image_io.cpp
  src/io/insv_reader.cpp
  src/io/frame_store.cpp
  src/io/video_writer.cpp)
target_include_directories(ps_io PUBLIC src)
target_link_libraries(ps_io PUBLIC ps_core ${OpenCV_LIBS})

if(PS_WITH_LIBRAW)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBRAW QUIET libraw)
  endif()
  if(LIBRAW_FOUND)
    target_compile_definitions(ps_io PUBLIC PS_HAVE_LIBRAW=1)
    target_include_directories(ps_io PRIVATE ${LIBRAW_INCLUDE_DIRS})
    target_link_directories(ps_io PRIVATE ${LIBRAW_LIBRARY_DIRS})
    target_link_libraries(ps_io PRIVATE ${LIBRAW_LIBRARIES})
    message(STATUS "LibRaw ${LIBRAW_VERSION}: DNG decoding enabled")
  else()
    message(WARNING "LibRaw not found: using built-in uncompressed CFA DNG reader")
  endif()
endif()

if(PS_WITH_FFMPEG)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(AV QUIET libavformat libavcodec libavutil libswscale)
  endif()
  if(AV_FOUND)
    target_compile_definitions(ps_io PUBLIC PS_HAVE_FFMPEG=1)
    target_include_directories(ps_io PRIVATE ${AV_INCLUDE_DIRS})
    target_link_directories(ps_io PRIVATE ${AV_LIBRARY_DIRS})
    target_link_libraries(ps_io PRIVATE ${AV_LIBRARIES})
    message(STATUS "libav ${AV_libavformat_VERSION}: INSV decoding enabled")
  else()
    message(WARNING "libav not found: INSV input disabled")
  endif()
endif()

add_executable(photosphere-cli src/cli/main.cpp)
target_link_libraries(photosphere-cli PRIVATE ps_io ps_core ${OpenCV_LIBS})

if(PS_BUILD_TESTS)
  enable_testing()
  add_executable(ps_tests tests/test_main.cpp
    tests/test_rotation.cpp
    tests/test_spherical.cpp
    tests/test_lens_model.cpp)
  target_link_libraries(ps_tests PRIVATE ps_core ps_io ${OpenCV_LIBS})
  add_test(NAME ps_tests COMMAND ps_tests)
endif()

if(PS_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
  qt_standard_project_setup()
  qt_add_executable(photosphere-gui
    src/gui/main.cpp
    src/gui/main_window.cpp
    src/gui/viewer_widget.cpp
    src/gui/export_dialog.cpp
    src/gui/general_settings.cpp
    src/gui/stitch_worker.cpp
    src/gui/video_export_worker.cpp
    src/gui/resources.qrc)
  target_link_libraries(photosphere-gui PRIVATE
    ps_io ps_core Qt6::Widgets Qt6::OpenGLWidgets)
  install(TARGETS photosphere-gui RUNTIME DESTINATION bin)
endif()

include(GNUInstallDirs)
install(TARGETS photosphere-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE THIRD_PARTY_LICENSES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/photosphere-linux)
install(DIRECTORY licenses/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/photosphere-linux)
install(FILES assets/photosphere-linux.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/icon.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
        RENAME photosphere-linux.png)
