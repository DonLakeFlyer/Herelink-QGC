message(STATUS "Adding Custom Plugin")

# Enable custom build
set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    APPEND PROPERTY COMPILE_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="HerelinkCorePlugin.h"
    CUSTOMCLASS=HerelinkCorePlugin
)

add_compile_definitions(
    QGC_HERELINK_AIRUNIT_VIDEO
)

if(ANDROID)
    set(CUSTOM_ANDROID_DIR "${CMAKE_SOURCE_DIR}/custom/android")
    if(EXISTS "${CUSTOM_ANDROID_DIR}")
        file(GLOB CUSTOM_ANDROID_FILES "${CUSTOM_ANDROID_DIR}/*")
        if(CUSTOM_ANDROID_FILES)
            message(STATUS "Custom Android package template found. Overlaying custom files...")
            set(DEFAULT_ANDROID_DIR "${CMAKE_SOURCE_DIR}/android")
            set(FINAL_ANDROID_DIR "${CMAKE_BINARY_DIR}/custom/android")
            file(COPY "${DEFAULT_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")
            file(COPY "${CUSTOM_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                            "${DEFAULT_ANDROID_DIR}/"
                            "${CUSTOM_ANDROID_DIR}/"
                        )
            set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${FINAL_ANDROID_DIR}" CACHE PATH "Path to a custom Android package template" FORCE)
            message(STATUS "Android package template path will be set to: ${QGC_ANDROID_PACKAGE_SOURCE_DIR}")
        else()
            message(STATUS "Custom Android package template empty. Using default.")
        endif()
    else()
        message(STATUS "No custom Android package template found. Using default.")
    endif()
endif()

find_package(Python3 REQUIRED)
execute_process(
    COMMAND ${Python3_EXECUTABLE}
    RESULT_VARIABLE PYTHON_RESULT
    OUTPUT_VARIABLE PYTHON_OUTPUT
    ERROR_VARIABLE PYTHON_ERROR
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    INPUT_FILE updateqrc.py
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
cmake_print_variables(PYTHON_RESULT PYTHON_OUTPUT PYTHON_ERROR)

# Our own, custom resources
list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qgcimages.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/qgcresources.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/qgroundcontrol.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${QGC_RESOURCES} ${CUSTOM_RESOURCES} CACHE STRING "Paths to .qrc Resources" FORCE)

set(QML_IMPORT_PATH ${QML_IMPORT_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/res" CACHE STRING "Extra qml import paths" FORCE)

set(CUSTOM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HerelinkCorePlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HerelinkCorePlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HerelinkOptions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HerelinkOptions.h
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_LIBRARIES
    Qt6::Core
    CACHE INTERNAL "" FORCE
)

#target_link_libraries(CustomPlugin
#    PRIVATE
#        FactSystem
#        FirmwarePlugin
#        PX4AutoPilotPlugin
#        PX4FirmwarePlugin
#        QmlControls
#        Vehicle
#    PUBLIC
#        Qt6::Core
#        MAVLink
#        QGC
#        Settings
#)

set(CUSTOM_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CACHE INTERNAL "" FORCE
)
