
set(include_dirs "${BOARD}/include")
set(srcs "${BOARD}/bsp_board.c")

if($ENV{BOARD} STREQUAL "esp-box")
    #set(requires "esp-box")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${requires})
