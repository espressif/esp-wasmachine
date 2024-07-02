if(CONFIG_WASMACHINE_WASM_EXT_NATIVE)
    set(srcs  "src/wm_ext_wasm_native.c" "src/wm_ext_wasm_native_common.c")
    set(include_dir "include")
    set(priv_include_dir "private_include")

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBC)
        list(APPEND srcs "src/wm_ext_wasm_native_libc.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBMATH)
        list(APPEND srcs "src/wm_ext_wasm_native_libm.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_HTTP_CLIENT)
        list(APPEND srcs "src/wm_ext_wasm_native_http_client.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL)
        list(APPEND srcs "src/wm_ext_wasm_native_lvgl.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_MQTT)
        list(APPEND srcs "src/wm_ext_wasm_native_mqtt.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
        list(APPEND srcs "src/wm_ext_wasm_native_rmaker.c")
    endif()

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING)
        list(APPEND srcs "src/wm_ext_wasm_native_wifi_provisioning.c")
    endif()
endif()

set(requires "wasmachine_ext_wasm_vfs" "wasm-micro-runtime")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES ${requires})

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE)
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_HTTP_CLIENT)
        idf_component_optional_requires(PRIVATE "esp_http_client")
    endif()
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL)
        idf_component_optional_requires(PRIVATE "espressif__lvgl")
        idf_component_optional_requires(PRIVATE "lvgl")
    endif()
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_MQTT)
        idf_component_optional_requires(PRIVATE "mqtt")
    endif()
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
        idf_component_optional_requires(PRIVATE "espressif__esp_rainmaker" "espressif__json_generator")
        idf_component_optional_requires(PRIVATE "esp_rainmaker" "json_generator")
    endif()
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING)
        idf_component_optional_requires(PRIVATE "wifi_provisioning")
    endif()
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if("espressif__esp_rainmaker" IN_LIST build_components)
        set(rainmaker_component_name espressif__esp_rainmaker)
    endif()
    if("esp_rainmaker" IN_LIST build_components)
        set(rainmaker_component_name esp_rainmaker)
    endif()
    idf_component_get_property(COMPONENT_DIR ${rainmaker_component_name} COMPONENT_DIR)
    target_include_directories(${COMPONENT_TARGET} PUBLIC "${COMPONENT_DIR}/src/core")
endif()
