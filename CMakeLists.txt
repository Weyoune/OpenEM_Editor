cmake_minimum_required(VERSION 3.24)
project(OpenEMEditor VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
# Static runtime on Windows: the editor runs without the Visual C++ redistributable
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /W3)
endif()
enable_testing()

# File formats of the original game (no UI): images, palettes, names, team tables, goal scenes, sounds
add_library(em_formats STATIC
    src/formats/binary.cpp
    src/formats/db.cpp
    src/formats/teamdata.cpp
    src/formats/tore.cpp
    src/formats/vga.cpp)
target_include_directories(em_formats PUBLIC src)

# Document logic of the editor (no UI, built and tested everywhere)
add_library(em_editor_lib STATIC
    src/editor/pixel_editor.cpp
    src/editor/palette_doc.cpp
    src/editor/team_doc.cpp
    src/editor/map_doc.cpp
    src/editor/scene_doc.cpp)
target_link_libraries(em_editor_lib PUBLIC em_formats)

add_executable(em_editor_tests tests/editor_tests.cpp)
target_link_libraries(em_editor_tests PRIVATE em_editor_lib)
add_executable(em_format_tests tests/format_tests.cpp)
target_link_libraries(em_format_tests PRIVATE em_formats)
# Pass the folder of the original game: cmake -S . -B build -DEM_DATA="<folder with EM.EXE>"
if(IS_DIRECTORY "${EM_DATA}")
    add_test(NAME editor_original COMMAND em_editor_tests "${EM_DATA}")
    add_test(NAME formats_original COMMAND em_format_tests "${EM_DATA}")
endif()

# Windows front end: native Win32 with Common Controls 6
if(WIN32)
    add_executable(em_editor WIN32 src/editor/win_main.cpp src/editor/em_editor.manifest)
    target_compile_definitions(em_editor PRIVATE UNICODE _UNICODE)
    target_link_libraries(em_editor PRIVATE em_editor_lib)
    install(TARGETS em_editor RUNTIME DESTINATION .)
endif()
install(FILES README.md LICENSE DESTINATION .)
install(DIRECTORY docs DESTINATION .)
