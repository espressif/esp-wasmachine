
set(srcs  "src/wm_wamr.c")
set(include_dir "include")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       REQUIRES "extended_wasm_native" "wamr")
