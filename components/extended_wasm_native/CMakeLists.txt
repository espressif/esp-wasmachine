if(CONFIG_WASMACHINE_WASM_EXT_NATIVE)
    set(srcs  "src/wm_ext_wasm_native.c" "src/wm_ext_wasm_native_common.c")
    set(include_dir "include")
    set(priv_include_dir "private_include")

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBC)
        list(APPEND srcs "src/wm_ext_wasm_native_libc.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_MQTT)
        list(APPEND srcs "src/wm_ext_wasm_native_mqtt.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_HTTP_CLIENT)
        list(APPEND srcs "src/wm_ext_wasm_native_http_client.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL)
        list(APPEND srcs "src/wm_ext_wasm_native_lvgl.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING)
        list(APPEND srcs "src/wm_ext_wasm_native_wifi_provisioning.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
        list(APPEND srcs "src/wm_ext_wasm_native_rmaker.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBMATH)
        list(APPEND srcs "src/wm_ext_wasm_native_libm.c")
    endif()
endif()

set(requires "extended_wasm_vfs" "mqtt" "wasm-micro-runtime"
             "lvgl" "wifi_provisioning" "esp_http_client"
             "esp_rainmaker" "json_generator")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES ${requires})

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
    idf_component_get_property(COMPONENT_DIR esp_rainmaker COMPONENT_DIR)
    target_include_directories(${COMPONENT_TARGET} PUBLIC
    "${COMPONENT_DIR}/src/core"
    )
endif()
