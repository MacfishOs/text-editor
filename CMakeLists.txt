# SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

project(cutefish-text-editor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 DBus Concurrent LinguistTools)
find_package(CutefishFramework REQUIRED COMPONENTS Application)

add_library(editor-core STATIC
    src/document.cpp
    src/fileoptions.cpp
    src/recentfiles.cpp
    src/searcher.cpp
    src/session.cpp
    src/syntax.cpp
    src/textbuffer.cpp
    src/textcodec.cpp
    src/undostack.cpp
)
target_include_directories(editor-core PUBLIC src)
# Linked into the application module.
set_target_properties(editor-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(editor-core PUBLIC Qt6::Core Qt6::Concurrent)

cutefish_add_application(${PROJECT_NAME} SOURCES
    src/main.cpp
    src/application.cpp
    src/editorview.cpp
    src/syntaxtheme.cpp
    resources.qrc
)
target_compile_definitions(${PROJECT_NAME}-module PRIVATE TEXT_EDITOR_VERSION="${PROJECT_VERSION}")
target_link_libraries(${PROJECT_NAME}-module PRIVATE
    editor-core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
)

file(GLOB TS_FILES translations/*.ts)
qt6_add_translation(QM_FILES ${TS_FILES})
add_custom_target(translations DEPENDS ${QM_FILES} SOURCES ${TS_FILES})
add_dependencies(${PROJECT_NAME} translations)

install(FILES cutefish-text-editor.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/cutefish-text-editor/translations)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
