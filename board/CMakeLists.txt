cmake_minimum_required(VERSION 3.16)

get_filename_component(PROJECT_DIR ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)
set(EXTRA_COMPONENT_DIRS ${PROJECT_DIR}/lib)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rbot)
