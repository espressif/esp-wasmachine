
set(include_dirs "include")
set(srcs "${BOARD}/bsp_board.c")

set(requires "esp-box")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${requires})
