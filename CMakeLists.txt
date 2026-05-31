cmake_minimum_required(VERSION 3.21)
project(eddy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

add_library(eddy_core STATIC
    src/cli.cpp
    src/config.cpp
    src/imageio.cpp
    src/items/arrowitem.cpp
    src/items/rectitem.cpp
    src/items/ellipseitem.cpp
    src/items/highlightitem.cpp
    src/items/redactitem.cpp
    src/items/penpathitem.cpp
    src/items/rasteritem.cpp
)
target_include_directories(eddy_core PUBLIC src)
target_link_libraries(eddy_core PUBLIC Qt6::Widgets)

add_executable(eddy src/main.cpp)
target_link_libraries(eddy PRIVATE eddy_core)

enable_testing()
function(eddy_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE eddy_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

eddy_test(test_cli)
eddy_test(test_config)
eddy_test(test_imageio)
eddy_test(test_items)
eddy_test(test_raster)
