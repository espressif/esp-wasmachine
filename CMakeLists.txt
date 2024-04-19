# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(CMAKE_HOST_WIN32)
    execute_process(COMMAND python3 tools/setup_components.py
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
else()
    execute_process(COMMAND python3 tools/setup_components.py
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_FILE /dev/stdout)
endif()

set(EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common")

if(DEFINED ENV{RMAKER_PATH})
  set(RMAKER_PATH $ENV{RMAKER_PATH})
else()
  set(RMAKER_PATH ${CMAKE_CURRENT_LIST_DIR}/components/esp-rainmaker)
endif()

list(APPEND EXTRA_COMPONENT_DIRS "${RMAKER_PATH}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wasmachine)