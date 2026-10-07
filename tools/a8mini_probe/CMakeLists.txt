add_executable(a8mini_probe main.cpp qml.qrc)
target_link_libraries(a8mini_probe PRIVATE a8video Qt5::QuickControls2)
if(APPLE)
    set_target_properties(a8mini_probe PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "A8 mini Probe"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.a8mini.probe")
elseif(WIN32)
    set_target_properties(a8mini_probe PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
