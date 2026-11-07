qt_add_library(mcstudio_ui STATIC
    CollectionCanvas.cpp
    CollectionCanvas.h
    CollectionWindow.cpp
    CollectionWindow.h
    CoverEditorDialog.cpp
    CoverEditorDialog.h
    DocumentArea.cpp
    DocumentArea.h
    GameDirectoryPage.cpp
    GameDirectoryPage.h
    IconFactory.cpp
    IconFactory.h
    ImageDocumentWidget.cpp
    ImageDocumentWidget.h
    InspectorPanel.cpp
    InspectorPanel.h
    MainWindow.cpp
    MainWindow.h
    NewProjectDialog.cpp
    NewProjectDialog.h
    OpenProjectDialog.cpp
    OpenProjectDialog.h
    PackEditorPage.cpp
    PackEditorPage.h
    PixelCanvas.cpp
    PixelCanvas.h
    PixelIcons.cpp
    PixelIcons.h
    PreferencesDialog.cpp
    PreferencesDialog.h
    ProjectOverviewWidget.cpp
    ProjectOverviewWidget.h
    ResourcePackImportDialog.cpp
    ResourcePackImportDialog.h
    TextureEditorPage.cpp
    TextureEditorPage.h
    TextDocumentWidget.cpp
    TextDocumentWidget.h
    TextureBoardPage.cpp
    TextureBoardPage.h
    TextureBoardView.cpp
    TextureBoardView.h
    Theme.cpp
    Theme.h
)

target_link_libraries(mcstudio_ui PUBLIC
    mcstudio_core
    Qt6::Gui
    Qt6::Widgets
)

target_include_directories(mcstudio_ui PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

qt_wrap_cpp(mcstudio_ui MainWindow.h)
