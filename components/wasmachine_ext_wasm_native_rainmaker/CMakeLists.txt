if(CONFIG_WASMACHINE_WASM_EXT_NATIVE)
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
        list(APPEND srcs "src/wm_ext_wasm_native_rmaker.c")
    endif()
endif()

set(requires "wasmachine_ext_wasm_native" "wasm-micro-runtime")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir}
                       PRIV_INCLUDE_DIRS ${priv_include_dir}
                       REQUIRES ${requires})

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_rmaker_export")
endif()

if(CONFIG_WASMACHINE_WASM_EXT_NATIVE)
    if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER)
        idf_component_optional_requires(PRIVATE "espressif__esp_rainmaker" "espressif__json_generator")
        idf_component_optional_requires(PRIVATE "esp_rainmaker" "json_generator")
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
