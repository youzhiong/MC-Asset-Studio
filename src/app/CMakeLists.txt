qt_add_executable(MCStudio WIN32
    main.cpp
)

target_link_libraries(MCStudio PRIVATE
    mcstudio_ui
)

set_target_properties(MCStudio PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "MCStudio"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:MCStudio>"
)

set(MCSTUDIO_ICON_PATH "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")
set(MCSTUDIO_MANIFEST_PATH "${CMAKE_SOURCE_DIR}/resources/app.manifest")
configure_file(
    "${CMAKE_SOURCE_DIR}/resources/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    @ONLY
)

target_sources(MCStudio PRIVATE
    "${CMAKE_SOURCE_DIR}/resources/resources.qrc"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
)
