qt_add_library(mcstudio_core STATIC
    AnimationMeta.cpp
    AnimationMeta.h
    AppPaths.cpp
    AppPaths.h
    AssetNamespace.cpp
    AssetNamespace.h
    AssetProvider.cpp
    AssetProvider.h
    AssetStack.cpp
    AssetStack.h
    ColorReducer.cpp
    ColorReducer.h
    AssociationCache.cpp
    AssociationCache.h
    AssociationTask.cpp
    AssociationTask.h
    ExportService.cpp
    ExportService.h
    GameEnvironment.cpp
    GameEnvironment.h
    GameScanTask.cpp
    GameScanTask.h
    GameSettings.cpp
    GameSettings.h
    InstanceInfo.cpp
    InstanceInfo.h
    ModCatalog.cpp
    ModCatalog.h
    ModCatalogTask.cpp
    ModCatalogTask.h
    ModInfo.cpp
    ModInfo.h
    ModScanner.cpp
    ModScanner.h
    PackFormat.cpp
    PackFormat.h
    PaletteStore.cpp
    PaletteStore.h
    PathUtils.h
    PixelEdit.cpp
    PixelEdit.h
    Project.cpp
    Project.h
    ProjectService.cpp
    ProjectService.h
    ReferenceEnvironment.cpp
    ReferenceEnvironment.h
    TextureAssociation.cpp
    TextureAssociation.h
    TextureCategory.cpp
    TextureCategory.h
    TextureDocument.cpp
    TextureDocument.h
    TemplateLibrary.cpp
    TemplateLibrary.h
    TintRecipe.cpp
    TintRecipe.h
    Version.cpp
    Version.h
    ZipArchive.cpp
    ZipArchive.h
    ZipImporter.cpp
    ZipImporter.h
)

target_link_libraries(mcstudio_core PUBLIC Qt6::Core Qt6::Gui)

# QZipReader/QZipWriter live in Qt's private Core headers. They are wrapped in
# ZipArchive/ExportService so a later switch to miniz/libzip is localized.
target_link_libraries(mcstudio_core PRIVATE Qt6::CorePrivate)

target_include_directories(mcstudio_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(mcstudio_core PRIVATE
    MCSTUDIO_VERSION="${PROJECT_VERSION}"
    MCSTUDIO_BUILD_TYPE="$<CONFIG>"
)
