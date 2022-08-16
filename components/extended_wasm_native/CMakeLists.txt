if(CONFIG_WASMACHINE_WASM_EXT_NATIVE)
    set(srcs  "src/wm_ext_wasm_native.c" "src/wm_ext_wasm_native_common.c")
    set(include_dir "include")
    set(priv_include_dir "private_include")

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBC)
        list(APPEND srcs "src/wm_ext_wasm_native_libc.c"
                         "src/wm_ext_wasm_native_vfs_ioctl.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_MQTT)
        list(APPEND srcs "src/wm_ext_wasm_native_mqtt.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_HTTP_CLIENT)
        list(APPEND srcs "src/wm_ext_wasm_native_http_client.c")
    endif()

    list(APPEND srcs "src/wm_ext_wasm_native_lvgl.c")

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING)
        list(APPEND srcs "src/wm_ext_wasm_native_wifi_provisioning.c")
    endif()
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES "data_sequence" "extended_vfs" "mqtt" "wamr" "boards" "lvgl" "wifi_provisioning" "esp_http_client")
