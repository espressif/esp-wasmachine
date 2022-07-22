# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(NOT BOARD)
    set(BOARD "esp-box")
endif()

message(STATUS "Choose board: ${BOARD}")

set(BOARD_PATH ${CMAKE_CURRENT_LIST_DIR}/components/boards/${BOARD})

# Check the ${BOARD} folder exists in components/boards.
if(EXISTS ${BOARD_PATH})
    list(APPEND EXTRA_COMPONENT_DIRS ${BOARD_PATH})

    set(SDKCONFIG_DEFAULTS_PATH ${BOARD_PATH}/sdkconfig.defaults)

    if(EXISTS "${SDKCONFIG_DEFAULTS_PATH}")
        set(SDKCONFIG_DEFAULTS ${SDKCONFIG_DEFAULTS_PATH})

        # It's a workaroud to set IDF_TARGET, since if sdkconfig.defaults is not under project folder,
        # the builder system cannot detect the IDF_TARGET.
        file(STRINGS ${SDKCONFIG_DEFAULTS_PATH} sdkconfig_defaults_contents)

        foreach(config_and_value ${sdkconfig_defaults_contents})
            # Strip spaces
            string(REPLACE " " "" config_and_value ${config_and_value})
            # Find config
            string(REGEX MATCH "^[^=]+" config ${config_and_value})
            # Find the value
            string(REGEX MATCH "[^=]+$" value ${config_and_value})
            # Getting values
            if("${config}" STREQUAL "CONFIG_IDF_TARGET")
                string(REPLACE "\"" "" value ${value})
                set(IDF_TARGET "${value}")
                break()
            endif()
        endforeach()
    endif()
else()
    message(FATAL_ERROR "Board ${BOARD} is not supported!")
endif()

list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common")

if(DEFINED ENV{RMAKER_PATH})
  set(RMAKER_PATH $ENV{RMAKER_PATH})
else()
  set(RMAKER_PATH ${CMAKE_CURRENT_LIST_DIR}/components/esp-rainmaker)
endif(DEFINED ENV{RMAKER_PATH})

list(APPEND EXTRA_COMPONENT_DIRS "${RMAKER_PATH}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wasmachine)