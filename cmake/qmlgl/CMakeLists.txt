# Compile the unmodified upstream Qt5 plugin against the application's Qt SDK.
# Keep GStreamer sources outside the repository; use the matching release tarball.
pkg_check_modules(GST_GL REQUIRED IMPORTED_TARGET gstreamer-gl-1.0 gstreamer-gl-prototypes-1.0)
set(qt_plugin_dir "${A8_QMLGL_SOURCE_DIR}/ext/qt")
if(NOT EXISTS "${qt_plugin_dir}/gstqtsink.cc")
    message(FATAL_ERROR "A8_QMLGL_SOURCE_DIR must point to a gst-plugins-good source tree")
endif()
set(plugin_sources gstplugin.cc gstqtelement.cc gstqsgtexture.cc gstqtglutility.cc
    gstqtoverlay.cc gstqtsink.cc gstqtsrc.cc qtglrenderer.cc qtitem.cc qtwindow.cc
    qtitem.h qtwindow.h gstqsgtexture.h qtglrenderer.h)
list(TRANSFORM plugin_sources PREPEND "${qt_plugin_dir}/")
add_library(gstqmlgl MODULE ${plugin_sources})
target_include_directories(gstqmlgl PRIVATE "${qt_plugin_dir}")
target_link_libraries(gstqmlgl PRIVATE Qt5::Quick PkgConfig::GST PkgConfig::GST_GL)
target_compile_definitions(gstqmlgl PRIVATE
    GST_PACKAGE_NAME="GStreamer\ Good\ Plug-ins"
    GST_PACKAGE_ORIGIN="https://gstreamer.freedesktop.org"
    GST_LICENSE="LGPL" PACKAGE="gst-plugins-good" PACKAGE_VERSION="${GST_gstreamer-1.0_VERSION}")
set_target_properties(gstqmlgl PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gst-plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gst-plugins")
if(APPLE)
    set_target_properties(gstqmlgl PROPERTIES SUFFIX ".dylib")
    find_package(Qt5 5.15 REQUIRED COMPONENTS MacExtras)
    target_compile_definitions(gstqmlgl PRIVATE HAVE_QT_MAC)
    target_link_libraries(gstqmlgl PRIVATE Qt5::MacExtras)
elseif(WIN32)
    target_compile_definitions(gstqmlgl PRIVATE HAVE_QT_WIN32)
    target_link_libraries(gstqmlgl PRIVATE opengl32)
else()
    message(FATAL_ERROR "The optional plugin builder currently supports macOS/Windows. Install your distro's qmlgl plugin on Linux.")
endif()
