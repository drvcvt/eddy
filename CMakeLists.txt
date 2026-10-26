cmake_minimum_required(VERSION 3.21)
project(eddy VERSION 1.0.3 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
# -O2, not CMake's -O3: the same speed here for less code (the whole program goes through LTO).
string(REPLACE "-O3" "-O2" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test Svg Multimedia MultimediaWidgets)
if(WIN32)
    find_package(Qt6 REQUIRED COMPONENTS Network)
endif()

if(UNIX AND NOT APPLE)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 COMPONENTS GuiPrivate QUIET)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(WAYLAND_CLIENT wayland-client IMPORTED_TARGET)
    endif()
endif()

add_library(eddy_core STATIC
    src/canvas/canvas.cpp
    src/canvas/previewitems.cpp
    src/edit/cropcontroller.cpp
    src/bars/cropbar.cpp
    src/chrome/motionicon.cpp
    src/timeline/zoombar.cpp
    src/timeline/fragmentbar.cpp
    src/output/exportsettings.cpp
    src/popovers/exportpanel.cpp
    src/project/projectcodec.cpp
    src/project/projectstore.cpp
    src/project/recoverystore.cpp
    src/popovers/resumedialog.cpp
    src/chrome/minimap.cpp
    src/chrome/loupe.cpp
    src/popovers/colorpopover.cpp
    src/bars/redactbar.cpp
    src/bars/textbar.cpp
    src/bars/spotlightbar.cpp
    src/timeline/videotimeline.cpp
    src/timeline/videotimeline_paint.cpp
    src/video/videopreviewprovider.cpp
    src/chrome/toast.cpp
    src/chrome/dragpill.cpp
    src/canvas/selectionhandles.cpp
    src/app/cli.cpp
    src/output/compositor.cpp
    src/app/config.cpp
    src/app/theme.cpp
    src/app/spring.cpp
    src/app/fills.cpp
    src/project/boltsnapipc.cpp
    src/editor/editorwindow.cpp
    src/editor/editorwindow_playbackbar.cpp
    src/editor/editorwindow_playback.cpp
    src/editor/editorwindow_bars.cpp
    src/editor/editorwindow_studio.cpp
    src/editor/editorwindow_video.cpp
    src/editor/editorwindow_output.cpp
    src/editor/editorwindow_project.cpp
    src/editor/editorwindow_keys.cpp
    src/output/exporter.cpp
    src/output/imageio.cpp
    src/video/mediaio.cpp
    src/video/cursortrack.cpp
    src/studio/studiostyle.cpp
    src/studio/studiopresets.cpp
    src/studio/studiodocument.cpp
    src/video/timemap.cpp
    src/studio/camerapath.cpp
    src/timeline/zoomlane.cpp
    src/video/fragments.cpp
    src/studio/zoomsuggest.cpp
    src/bars/stepbar.cpp
    src/canvas/snapping.cpp
    src/bars/selectionbar.cpp
    src/timeline/audiowaveform.cpp
    src/studio/studiorenderer.cpp
    src/output/renderexport.cpp
    src/popovers/studiopopover.cpp
    src/video/videoexporter.cpp
    src/project/waylanddrag.cpp
    src/edit/ocr.cpp
    src/edit/redactocrcontroller.cpp
    src/edit/undocommands.cpp
    src/edit/toolcontroller.cpp
    src/chrome/toolbar.cpp
    src/items/shapes/arrowitem.cpp
    src/items/shapes/rectitem.cpp
    src/items/shapes/ellipseitem.cpp
    src/items/shapes/highlightitem.cpp
    src/items/regions/redactitem.cpp
    src/items/shapes/penpathitem.cpp
    src/items/regions/rasteritem.cpp
    src/items/labels/textitem.cpp
    src/items/labels/stepitem.cpp
    src/items/regions/spotlightitem.cpp
    resources/eddy.qrc
)
target_include_directories(eddy_core PUBLIC src)
# Compress every resource that shrinks by 30 %, not only by rcc's default 70 %:
# the bundled font (54 % smaller) was stored raw.
set_target_properties(eddy_core PROPERTIES AUTORCC_OPTIONS "--compress;9;--threshold;30")
target_compile_definitions(eddy_core PRIVATE EDDY_VERSION="${PROJECT_VERSION}")
target_link_libraries(eddy_core PUBLIC Qt6::Widgets Qt6::Svg Qt6::Multimedia Qt6::MultimediaWidgets)
if(WIN32)
    target_link_libraries(eddy_core PRIVATE Qt6::Network dwmapi)
endif()
if(WAYLAND_CLIENT_FOUND AND TARGET Qt6::GuiPrivate AND Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    target_link_libraries(eddy_core PRIVATE PkgConfig::WAYLAND_CLIENT Qt6::GuiPrivate)
    target_compile_definitions(eddy_core PRIVATE EDDY_HAVE_WAYLAND_CLIENT=1)
endif()

add_executable(eddy src/app/main.cpp)
target_link_libraries(eddy PRIVATE eddy_core)
if(WIN32)
    enable_language(RC)
    target_sources(eddy PRIVATE resources/eddy.rc)
    set_target_properties(eddy PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(NOT MSVC)
    # Release strips symbols and packs relocations; nothing links what it doesn't call.
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,-z,pack-relative-relocs" EDDY_PACK_RELOCS)
    target_link_options(eddy PRIVATE -Wl,--as-needed $<$<CONFIG:Release>:-s>
        $<$<BOOL:${EDDY_PACK_RELOCS}>:-Wl,-z,pack-relative-relocs>)
endif()

install(TARGETS eddy RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES resources/eddy.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/eddy.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
if(WIN32)
    qt_generate_deploy_app_script(
        TARGET eddy
        OUTPUT_SCRIPT eddy_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${eddy_deploy_script})
endif()

add_executable(eddy_preview tools/eddy_preview.cpp)
target_link_libraries(eddy_preview PRIVATE eddy_core)

enable_testing()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME layout COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/check_layout.py ${CMAKE_SOURCE_DIR})
endif()
function(eddy_test path)
    get_filename_component(name ${path} NAME)
    add_executable(${name} tests/${path}.cpp)
    target_link_libraries(${name} PRIVATE eddy_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name} -o -,txt -o ${name}.txt,txt)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion")
endfunction()

eddy_test(canvas/test_canvas)
eddy_test(canvas/test_canvaszoom)
eddy_test(canvas/test_previewitems)
eddy_test(chrome/test_loupe)
eddy_test(popovers/test_colorpopover)
eddy_test(bars/test_redactbar)
eddy_test(timeline/test_zoombar)
eddy_test(chrome/test_minimap)
eddy_test(chrome/test_toast)
eddy_test(chrome/test_dragpill)
eddy_test(app/test_cli)
eddy_test(app/test_config)
eddy_test(video/test_mediaio)
eddy_test(video/test_cursortrack)
eddy_test(studio/test_studiostyle)
eddy_test(popovers/test_studiopopover)
eddy_test(studio/test_studiopresets)
eddy_test(studio/test_studiodocument)
eddy_test(video/test_timemap)
eddy_test(studio/test_camerapath)
eddy_test(timeline/test_zoomlane)
eddy_test(video/test_fragments)
eddy_test(studio/test_zoomsuggest)
eddy_test(timeline/test_audiowaveform)
eddy_test(items/test_stepitem)
eddy_test(canvas/test_snapping)
eddy_test(studio/test_studiorenderer)
eddy_test(output/test_renderexport)
eddy_test(output/test_exporter)
eddy_test(project/test_boltsnapipc)
eddy_test(video/test_videoexporter)
eddy_test(video/test_videorender)
eddy_test(output/test_exportsettings)
eddy_test(popovers/test_exportpanel)
eddy_test(project/test_projectcodec)
eddy_test(project/test_projectstore)
eddy_test(project/test_recoverystore)
eddy_test(output/test_imageio)
eddy_test(items/test_items)
eddy_test(edit/test_ocr)
eddy_test(edit/test_redactocrcontroller)
eddy_test(items/test_raster)
eddy_test(edit/test_toolcontroller)
eddy_test(output/test_compositor)
eddy_test(chrome/test_toolbar)
eddy_test(editor/test_editorwindow)
eddy_test(editor/test_editorvideo)
eddy_test(editor/test_editorvideoout)
eddy_test(editor/test_uigates)
eddy_test(studio/test_studiozooms)
eddy_test(project/test_projects)
eddy_test(timeline/test_fragmentsedit)
eddy_test(app/test_theme)
eddy_test(app/test_motion)
eddy_test(canvas/test_selectionhandles)
eddy_test(timeline/test_videotimeline)
eddy_test(video/test_videopreviewprovider)

eddy_test(edit/test_crop)
