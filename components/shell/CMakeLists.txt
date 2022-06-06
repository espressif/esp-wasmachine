if(CONFIG_WASMACHINE_SHELL)
    set(srcs  "src/shell_init.c")
    set(include_dir "include")
    set(priv_include_dir "private_include")

    if(CONFIG_WASMACHINE_SHELL_CMD_IWASM)
        list(APPEND srcs "src/shell_iwasm.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_LS)
        list(APPEND srcs "src/shell_ls.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_FREE)
        list(APPEND srcs "src/shell_free.c")
    endif()
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES "wamr" "console" "wasmachine")
