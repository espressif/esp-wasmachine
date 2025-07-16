if(CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL)
    idf_build_get_property(project_dir PROJECT_DIR)
    set(lvgl_dir "${project_dir}/managed_components/lvgl__lvgl")
    message(STATUS "LVGL directory: ${lvgl_dir}")

    if(EXISTS ${lvgl_dir})
        # Apply patches to managed components
        set(tools_dir "${COMPONENT_DIR}/tools")
        message(STATUS "Tools directory: ${tools_dir}")

        set(PATCH_SCRIPT "${tools_dir}/apply_patch.py")
        set(LVGL_PATCH "${tools_dir}/patches/lvgl.patch")
        set(LVGL_TARGET "${lvgl_dir}")

        # Check if python patch package is available
        execute_process(
            COMMAND python3 -c "import patch; print('patch package is available')"
            RESULT_VARIABLE PATCH_CHECK_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(NOT PATCH_CHECK_RESULT EQUAL 0)
            message(WARNING "Python patch package not found. Installing via pip...")
            execute_process(
                COMMAND python3 -m pip install patch
                RESULT_VARIABLE PATCH_INSTALL_RESULT
            )
            if(NOT PATCH_INSTALL_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to install python patch package")
            endif()
        endif()

        execute_process(
            COMMAND python3 "${PATCH_SCRIPT}" --patch "${LVGL_PATCH}" --target "${LVGL_TARGET}"
            RESULT_VARIABLE result
            ERROR_VARIABLE error
        )

        if(NOT result EQUAL 0)
            message(WARNING "Failed to apply patches: ${error} ${result}")
        endif()
    else()
        message(FATAL_ERROR "LVGL directory not found")
    endif()
else()
    message(STATUS "LVGL is not enabled")
endif()
