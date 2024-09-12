message(STATUS "Adding Custom Plugin")
find_package(Qt6 REQUIRED COMPONENTS Core)

# Enable custom build
set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    APPEND PROPERTY COMPILE_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="HerelinkCorePlugin.h"
    CUSTOMCLASS=HerelinkCorePlugin
)

# Our own, custom resources
list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qgcimages.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/qgcresources.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/qgroundcontrol.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${CUSTOM_RESOURCES} PARENT_SCOPE)

set(QML_IMPORT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/res" CACHE STRING "Extra qml import paths" PARENT_SCOPE)

qt_add_library(CustomPlugin STATIC
    src/HerelinkCorePlugin.cc
    src/HerelinkCorePlugin.h
    src/HerelinkOptions.cc
    src/HerelinkOptions.h
)

target_link_libraries(CustomPlugin
    PRIVATE
        FactSystem
        FirmwarePlugin
        PX4AutoPilotPlugin
        PX4FirmwarePlugin
        QmlControls
        Vehicle
    PUBLIC
        Qt6::Core
        MAVLink
        QGC
        Settings
)

target_include_directories(CustomPlugin
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
