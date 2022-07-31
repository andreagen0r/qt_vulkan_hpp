cmake_minimum_required(VERSION 3.16)
project(MyRender LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Qml Gui Quick)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -pedantic")

add_executable(${PROJECT_NAME}
    main.cpp
    customtexturenode.h customtexturenode.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::Qml
    Qt::Quick
    Vulkan::Vulkan
)

qt_add_qml_module(${PROJECT_NAME}
    URI MyVKRender
    VERSION 1.0
    QML_FILES
        main.qml
    RESOURCES
        squircle.frag.spv
        squircle.vert.spv
    NO_RESOURCE_TARGET_PATH
)
