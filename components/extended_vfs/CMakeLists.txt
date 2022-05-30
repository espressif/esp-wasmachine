if(CONFIG_WASMACHINE_EXT_VFS)
    set(srcs  "src/wm_ext_vfs.c")
    set(include_dir "include")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir})