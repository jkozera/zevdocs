cmake_minimum_required(VERSION 3.10)
project(zevdocs C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
# from https://github.com/savoirfairelinux/ring-client-gnome/blob/master/cmake/GResources.cmake
FIND_PROGRAM(GLIB_COMPILE_RESOURCES_EXECUTABLE NAMES glib-compile-resources)
MARK_AS_ADVANCED(GLIB_COMPILE_RESOURCES_EXECUTABLE)

INCLUDE(CMakeParseArguments)

FUNCTION(GLIB_COMPILE_RESOURCES output)
    CMAKE_PARSE_ARGUMENTS(ARGS "" "SOURCE" ${ARGN})
    SET(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    SET(out_files "")

    FOREACH(src ${ARGS_SOURCE} ${ARGS_UNPARSED_ARGUMENTS})
        SET(in_file "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        GET_FILENAME_COMPONENT(WORKING_DIR ${in_file} PATH)
        STRING(REPLACE ".xml" ".c" src ${src})
        SET(out_file "${DIRECTORY}/${src}")
        GET_FILENAME_COMPONENT(OUPUT_DIR ${out_file} PATH)
        FILE(MAKE_DIRECTORY ${OUPUT_DIR})
        LIST(APPEND out_files "${DIRECTORY}/${src}")

        #FIXME implicit depends currently not working
        EXECUTE_PROCESS(
                COMMAND
                ${GLIB_COMPILE_RESOURCES_EXECUTABLE}
                "--generate-dependencies"
                ${in_file}
                WORKING_DIRECTORY ${WORKING_DIR}
                OUTPUT_VARIABLE in_file_dep
        )
        STRING(REGEX REPLACE "(\r?\n)" ";" in_file_dep "${in_file_dep}")
        SET(in_file_dep_path "")
        FOREACH(dep ${in_file_dep})
            LIST(APPEND in_file_dep_path "${WORKING_DIR}/${dep}")
        ENDFOREACH(dep ${in_file_dep})
        ADD_CUSTOM_COMMAND(
                OUTPUT ${out_file}
                WORKING_DIRECTORY ${WORKING_DIR}
                COMMAND
                ${GLIB_COMPILE_RESOURCES_EXECUTABLE}
                ARGS
                "--generate-source"
                "--target=${out_file}"
                ${in_file}
                DEPENDS
                ${in_file};${in_file_dep_path}
        )
    ENDFOREACH(src ${ARGS_SOURCES} ${ARGS_UNPARSED_ARGUMENTS})
    SET(${output} ${out_files} PARENT_SCOPE)
ENDFUNCTION(GLIB_COMPILE_RESOURCES)


set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GDK REQUIRED gdk-3.0)
pkg_check_modules(Cairo REQUIRED cairo)
pkg_check_modules(Webkit REQUIRED webkit2gtk-4.0)
pkg_check_modules(jsonglib REQUIRED json-glib-1.0)

include_directories(build/)
include_directories(${GTK3_INCLUDE_DIRS})
include_directories(${GLIB_INCLUDE_DIRS})
include_directories(${GDK_INCLUDE_DIRS})
include_directories(${Cairo_INCLUDE_DIRS})
include_directories(${Webkit_INCLUDE_DIRS})
include_directories(${jsonglib_INCLUDE_DIRS})

include_directories(_build)
include_directories(.)
include_directories(devhelp/future)
include_directories(src)

GLIB_COMPILE_RESOURCES( GLIB_RESOURCES_ZEVDOCS
        SOURCE
        src/dh.gresource.xml
        devhelp/dh-lib.gresource.xml
        )

find_package(Vala "0.40" REQUIRED)


list(APPEND VALA_COMPILER_FLAGS "--pkg=libsoup-2.4")
list(APPEND VALA_COMPILER_FLAGS "--pkg=json-glib-1.0")
list(APPEND VALA_COMPILER_FLAGS "--gresources=${CMAKE_CURRENT_SOURCE_DIR}/devhelp/dh-lib.gresource.xml")
list(APPEND VALA_COMPILER_FLAGS '-X')
list(APPEND VALA_COMPILER_FLAGS '-DGETTEXT_PACKAGE="zevdocs"')
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D \"GETTEXT_PACKAGE=\\\"zevdocs\\\"\"")


vala_precompile_target(
        "libdevhelp-vala"
        VALA_SOURCES_ZEVDOCS
        devhelp/dh-groupdialog.vala
        devhelp/dh-profile-chooser.vala
        PACKAGES "gtk+-3.0"
        # make it compatible with 'meson . build' by placing the header in build/:
        HEADER ${CMAKE_CURRENT_SOURCE_DIR}/build/devhelp/devhelp-vala.h)

add_executable(zevdocs
        ${GLIB_RESOURCES_ZEVDOCS}
        devhelp/devhelp.h
        devhelp/dh-application-window.c
        devhelp/dh-application-window.h
        devhelp/dh-assistant-view.c
        devhelp/dh-assistant-view.h
        devhelp/dh-book-list-builder.c
        devhelp/dh-book-list-builder.h
        devhelp/dh-book-list-directory.c
        devhelp/dh-book-list-directory.h
        devhelp/dh-book-list-simple.c
        devhelp/dh-book-list-simple.h
        devhelp/dh-book-list.c
        devhelp/dh-book-list.h
        devhelp/dh-book-manager.c
        devhelp/dh-book-manager.h
        devhelp/dh-book-tree-model.c
        devhelp/dh-book-tree-model.h
        devhelp/dh-book-tree.c
        devhelp/dh-book-tree.h
        devhelp/dh-book.c
        devhelp/dh-book.h
        devhelp/dh-completion.c
        devhelp/dh-completion.h
        devhelp/dh-error.c
        devhelp/dh-error.h
        devhelp/dh-init.c
        devhelp/dh-init.h
        devhelp/dh-keyword-model.c
        devhelp/dh-keyword-model.h
        devhelp/dh-link.c
        devhelp/dh-link.h
        devhelp/dh-notebook.c
        devhelp/dh-notebook.h
        devhelp/dh-parser.c
        devhelp/dh-parser.h
        devhelp/dh-profile-builder.c
        devhelp/dh-profile-builder.h
        devhelp/dh-profile.c
        devhelp/dh-profile.h
        devhelp/dh-search-bar.c
        devhelp/dh-search-bar.h
        devhelp/dh-search-context.c
        devhelp/dh-search-context.h
        devhelp/dh-settings-builder.c
        devhelp/dh-settings-builder.h
        devhelp/dh-settings.c
        devhelp/dh-settings.h
        devhelp/dh-sidebar.c
        devhelp/dh-sidebar.h
        devhelp/dh-tab-label.c
        devhelp/dh-tab-label.h
        devhelp/dh-tab.c
        devhelp/dh-tab.h
        devhelp/dh-util-lib.c
        devhelp/dh-util-lib.h
        devhelp/dh-web-view.c
        devhelp/dh-web-view.h
        devhelp/dh-groupdialog.vala
        src/dh-app.c
        src/dh-app.h
        src/dh-assistant.c
        src/dh-assistant.h
        src/dh-main.c
        src/dh-preferences.c
        src/dh-preferences.h
        src/dh-settings-app.c
        src/dh-settings-app.h
        src/dh-util-app.c
        src/dh-util-app.h
        src/dh-window.c
        src/dh-window.h
        ${VALA_SOURCES_ZEVDOCS})

# Make sure the Vala sources are compiled to C before attempting to
# build the executable.
add_dependencies("zevdocs" "libdevhelp-vala")

target_link_libraries(${PROJECT_NAME} ${GLIB_LIBRARIES})
target_link_libraries(${PROJECT_NAME} ${Webkit_LIBRARIES})
target_link_libraries(${PROJECT_NAME} ${jsonglib_LIBRARIES})
target_link_libraries(${PROJECT_NAME} ${amtk_LIBRARIES})
target_link_libraries(${PROJECT_NAME} m)
