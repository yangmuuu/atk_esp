# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# if(NOT DEFINED ENV{ADF_PATH})
#     message(FATAL_ERROR "Please define the ADF_PATH environment variable.")
# endif()

# include($ENV{ADF_PATH}/CMakeLists.txt)

# 添加库目录
# new
# set(EXTRA_COMPONENT_DIRS components/Middlewares) 
# set(EXTAR_COMPONENTS_DIRS components/Middlewares)

list(APPEND EXTRA_COMPONENT_DIRS "components/Middlewares")

# 报错带颜色
add_compile_options(-fdiagnostics-color=always)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifi_demo)
