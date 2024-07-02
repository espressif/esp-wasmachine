if(CONFIG_WASMACHINE_SHELL)
    set(srcs  "src/shell_init.c" "src/shell_utils.c")
    set(include_dir "include")
    set(priv_include_dir "private_include")

    if(CONFIG_WASMACHINE_SHELL_CMD_IWASM)
        list(APPEND srcs "src/shell_iwasm.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_INSTALL)
        list(APPEND srcs "src/shell_install.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_UNINSTALL)
        list(APPEND srcs "src/shell_uninstall.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_QUERY)
        list(APPEND srcs "src/shell_query.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_LS)
        list(APPEND srcs "src/shell_ls.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_FREE)
        list(APPEND srcs "src/shell_free.c")
    endif()

    if(CONFIG_WASMACHINE_SHELL_CMD_WIFI)
        list(APPEND srcs "src/shell_wifi.c")
    endif()
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES "esp_wifi" "wasm-micro-runtime" "console" "wasmachine_core")
