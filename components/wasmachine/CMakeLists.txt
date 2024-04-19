
set(srcs  "src/wm_wamr.c")
set(include_dir "include")

if(CONFIG_WASMACHINE_APP_MGR)
    list(APPEND srcs "src/wm_wamr_app_mgr.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       REQUIRES "extended_wasm_native" "wasm-micro-runtime" "extended_wasm_vfs")
