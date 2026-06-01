cmake_minimum_required(VERSION 3.21)
project(eddy LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test Svg)

add_library(eddy_core STATIC
    src/canvas.cpp
    src/colorpopover.cpp
    src/redactbar.cpp
    src/toast.cpp
    src/selectionhandles.cpp
    src/cli.cpp
    src/compositor.cpp
    src/config.cpp
    src/theme.cpp
    src/editorwindow.cpp
    src/exporter.cpp
    src/imageio.cpp
    src/ocr.cpp
    src/redactocrcontroller.cpp
    src/undocommands.cpp
    src/toolcontroller.cpp
    src/toolbar.cpp
    src/items/arrowitem.cpp
    src/items/rectitem.cpp
    src/items/ellipseitem.cpp
    src/items/highlightitem.cpp
    src/items/redactitem.cpp
    src/items/penpathitem.cpp
    src/items/rasteritem.cpp
    src/items/textitem.cpp
    resources/eddy.qrc
)
target_include_directories(eddy_core PUBLIC src)
target_link_libraries(eddy_core PUBLIC Qt6::Widgets Qt6::Svg)

add_executable(eddy src/main.cpp)
target_link_libraries(eddy PRIVATE eddy_core)
target_compile_options(eddy PRIVATE $<$<CONFIG:Release>:-O2>)

add_executable(eddy_preview tools/eddy_preview.cpp)
target_link_libraries(eddy_preview PRIVATE eddy_core)

enable_testing()
function(eddy_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE eddy_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

eddy_test(test_canvas)
eddy_test(test_colorpopover)
eddy_test(test_redactbar)
eddy_test(test_toast)
eddy_test(test_cli)
eddy_test(test_config)
eddy_test(test_exporter)
eddy_test(test_imageio)
eddy_test(test_items)
eddy_test(test_ocr)
eddy_test(test_redactocrcontroller)
eddy_test(test_raster)
eddy_test(test_toolcontroller)
eddy_test(test_compositor)
eddy_test(test_toolbar)
eddy_test(test_editorwindow)
eddy_test(test_theme)
eddy_test(test_selectionhandles)
