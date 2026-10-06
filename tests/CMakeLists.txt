# SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE editor-core Qt6::Test)
add_test(NAME text_editor_core COMMAND tst_core)

add_executable(tst_ui
    tst_ui.cpp
    ${PROJECT_SOURCE_DIR}/src/application.cpp
    ${PROJECT_SOURCE_DIR}/src/editorview.cpp
    ${PROJECT_SOURCE_DIR}/src/syntaxtheme.cpp
    ${PROJECT_SOURCE_DIR}/resources.qrc
)
target_include_directories(tst_ui PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(tst_ui PRIVATE editor-core Cutefish::Application
    Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Test)
add_test(NAME text_editor_ui COMMAND tst_ui -platform offscreen)
