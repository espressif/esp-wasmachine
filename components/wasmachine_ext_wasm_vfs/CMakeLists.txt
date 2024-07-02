if(CONFIG_WASMACHINE_EXT_VFS)
    set(srcs  "src/wm_ext_wasm_vfs.c"
              "src/wm_ext_wasm_vfs_ioctl.c")
    set(include_dir "include")
    set(priv_include_dir "private_include")
endif()

set(requires "extended_vfs" "wasm-micro-runtime" "wasmachine_data_sequence" "wasmachine_ext_wasm_native" "vfs" "driver")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES ${requires})
