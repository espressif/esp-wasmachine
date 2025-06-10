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

    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING)
        list(APPEND srcs "src/wm_ext_wasm_native_wifi_provisioning.c")
    endif()
endif()

set(requires "wasmachine_ext_wasm_vfs" "wasm-micro-runtime")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES ${requires}
                       LDFRAGMENTS linker.lf)

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBC)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_libc_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBMATH)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_libm_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_HTTP_CLIENT)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_http_client_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_lvgl_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_MQTT)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_mqtt_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_rmaker_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_wifi_provisioning_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE)
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_HTTP_CLIENT)
        idf_component_optional_requires(PRIVATE "esp_http_client")
    endif()
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL)
        idf_component_optional_requires(PRIVATE "lvgl__lvgl")
    endif()
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_MQTT)
        idf_component_optional_requires(PRIVATE "mqtt")
    endif()
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING)
        # Network Provisioning component is supported for IDF v5.1+
        idf_component_optional_requires(PRIVATE "network_provisioning")
    endif()
endif()
