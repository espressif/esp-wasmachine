
set(srcs  "src/wm_wamr.c")
set(include_dir "include")

if(CONFIG_WASMACHINE_APP_MGR)
    list(APPEND srcs "src/wm_wamr_app_mgr.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       REQUIRES "wasm-micro-runtime" "wasmachine_ext_wasm_native" "wasmachine_ext_wasm_vfs")
