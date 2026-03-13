cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(dgiot)

#add_definitions(-Werror)
#add_definitions(-Wno-error=deprecated-declarations)
# Keep compiler options out of add_definitions so newer CMake versions pass them through correctly.
add_compile_options(-O3 -w -Wno-psabi)
option(release "compile for release" ON)
option(debug "compile for debug" OFF)

option(RC0240 "compile for RC0240 Board" OFF)
option(RC0240_LGV10 "compile for RC0240_LGV10 Board" OFF)

option(ATBM6052 "compile for ATBM6052" OFF)
option(ATBM6012B "compile for ATBM6012B" OFF)

#系统
SET(CMAKE_SYSTEM_NAME Linux)

#指定编译器
#c编译
SET(CMAKE_C_COMPILER "arm-rockchip830-linux-uclibcgnueabihf-gcc")
#c++编译
SET(CMAKE_CXX_COMPILER "arm-rockchip830-linux-uclibcgnueabihf-g++")

#PROJECT_SOURCE_DIR：工程的根目录
#PROJECT_BINARY_DIR：运行cmake命令的目录，通常为${PROJECT_SOURCE_DIR}/build
#PROJECT_NAME：返回通过 project 命令定义的项目名称
#CMAKE_CURRENT_SOURCE_DIR：当前处理的 CMakeLists.txt 所在的路径
#CMAKE_CURRENT_BINARY_DIR：target 编译目录
#CMAKE_CURRENT_LIST_DIR：CMakeLists.txt 的完整路径
#EXECUTABLE_OUTPUT_PATH：重新定义目标二进制可执行文件的存放位置
#LIBRARY_OUTPUT_PATH：重新定义目标链接库文件的存放位置


# 添加子目录
add_subdirectory(libmpp)

