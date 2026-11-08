cmake_minimum_required(VERSION 3.16)
project(arch-housekeeper VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_executable(arch-housekeeper
    src/main.cpp
    src/core.cpp         src/core.h
    src/inspect.cpp      src/inspect.h
    src/packagetable.cpp src/packagetable.h
    src/widgets.h
    src/mainwindow.cpp   src/mainwindow.h
    src/details.cpp
    src/configtab.cpp
    src/leftoverstab.cpp
)
target_link_libraries(arch-housekeeper PRIVATE Qt6::Widgets)
target_compile_options(arch-housekeeper PRIVATE -Wall -Wextra)

include(GNUInstallDirs)
install(TARGETS arch-housekeeper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES arch-housekeeper.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
