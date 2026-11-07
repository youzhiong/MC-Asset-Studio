cmake_minimum_required(VERSION 3.24)

project(MCStudio VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Keep all executables in one predictable place for scripts and packaging.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
# QZipReader/QZipWriter are private QtCore APIs; the wrapper is isolated in
# ZipArchive/ExportService. Acknowledge the version coupling explicitly.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6CorePrivate REQUIRED)
qt_standard_project_setup()

# This project is built from a confined terminal in some environments where
# Qt's AutoMoc helper cannot spawn the compiler.  Use an explicit moc step for
# the small number of Q_OBJECT headers instead; it is also easier to inspect.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

# MSBuild's file tracker (Detours-based) cannot attach to the compiler when
# this project is built from a restricted file sandbox.  These are project
# globals, so `cmake --build` and a plain MSBuild invocation both work.
if(MSVC)
    set(CMAKE_VS_GLOBALS "TrackFileAccess=false;UseMultiToolTask=false")
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/app)
