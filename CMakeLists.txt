cmake_minimum_required(VERSION 3.21)
project(Tabor8200-Control LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network LinguistTools)
qt_standard_project_setup()
set(QT_I18N_SOURCE_LANGUAGE de)
set(CMAKE_AUTORCC ON)

qt_add_executable(Tabor8200-Control WIN32
    main.cpp mainwindow.cpp mainwindow.h updates.cpp
    tabor8200.cpp tabor8200.h gpib.cpp gpib.h version.h
    assets/app.qrc assets/app.rc
)
target_link_libraries(Tabor8200-Control PRIVATE Qt6::Widgets Qt6::Network)
target_include_directories(Tabor8200-Control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_definitions(Tabor8200-Control PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(Tabor8200-Control PRIVATE /utf-8)
endif()
qt_add_translations(Tabor8200-Control TS_FILES translations/tabor8200_en.ts translations/tabor8200_fr.ts)
foreach(locale IN ITEMS de fr)
    set(qt_catalog "${Qt6_DIR}/../../../translations/qtbase_${locale}.qm")
    if(EXISTS "${qt_catalog}")
        get_filename_component(qt_catalog "${qt_catalog}" ABSOLUTE)
        set_source_files_properties("${qt_catalog}" PROPERTIES QT_RESOURCE_ALIAS "qtbase_${locale}.qm")
        qt_add_resources(Tabor8200-Control "qt_i18n_${locale}" PREFIX "/qt-i18n" FILES "${qt_catalog}")
    endif()
endforeach()
