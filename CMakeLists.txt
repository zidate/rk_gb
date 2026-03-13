cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

# Select the default RK830 cross toolchain before project() so CMake compiler
# detection and generated rules stay consistent across 3.16.x and newer.
set(CMAKE_SYSTEM_NAME Linux)

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "arm-rockchip830-linux-uclibcgnueabihf-gcc" CACHE FILEPATH "Default RK830 C compiler")
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "arm-rockchip830-linux-uclibcgnueabihf-g++" CACHE FILEPATH "Default RK830 CXX compiler")
endif()

project(dgiot)

#add_definitions(-Werror)
#add_definitions(-Wno-error=deprecated-declarations)
# Keep compiler options out of add_definitions so newer CMake versions pass them through correctly.
add_compile_options(-O3 -w -Wno-psabi)
option(release "compile for release" ON)
option(debug "compile for debug" OFF)

option(ATBM6052 "compile for ATBM6052" OFF)
option(ATBM6012B "compile for ATBM6012B" OFF)
option(ATBM6132 "compile for ATBM6132" OFF)
option(AIC8800DL "compile for AIC8800DL" OFF)

get_filename_component(RK_GB_C_COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)
get_filename_component(RK_GB_CXX_COMPILER_NAME "${CMAKE_CXX_COMPILER}" NAME)

if(NOT RK_GB_C_COMPILER_NAME STREQUAL "arm-rockchip830-linux-uclibcgnueabihf-gcc"
   OR NOT RK_GB_CXX_COMPILER_NAME STREQUAL "arm-rockchip830-linux-uclibcgnueabihf-g++")
    message(FATAL_ERROR
        "Unexpected compiler selection:\n"
        "  C   = ${CMAKE_C_COMPILER}\n"
        "  CXX = ${CMAKE_CXX_COMPILER}\n"
        "This project expects the RK830 cross toolchain. "
        "Use a fresh build directory, or pass the desired compiler paths on the first CMake configure.")
endif()

# 添加子目录

add_subdirectory(Package)
add_subdirectory(App)

#PROJECT_SOURCE_DIR：工程的根目录
#PROJECT_BINARY_DIR：运行cmake命令的目录，通常为${PROJECT_SOURCE_DIR}/build
#PROJECT_NAME：返回通过 project 命令定义的项目名称
#CMAKE_CURRENT_SOURCE_DIR：当前处理的 CMakeLists.txt 所在的路径
#CMAKE_CURRENT_BINARY_DIR：target 编译目录
#CMAKE_CURRENT_LIST_DIR：CMakeLists.txt 的完整路径
#EXECUTABLE_OUTPUT_PATH：重新定义目标二进制可执行文件的存放位置
#LIBRARY_OUTPUT_PATH：重新定义目标链接库文件的存放位置
