if (NOT CMAKE_BUILD_EARLY_EXPANSION)
    # WAMR Configuration

    set(WAMR_BUILD_PLATFORM "esp-idf")

    set (WAMR_APP_THREAD_STACK_SIZE_MAX ${CONFIG_WAMR_APP_THREAD_STACK_SIZE_MAX})

    if(CONFIG_WAMR_ENABLE_INTERP)
        set(WAMR_BUILD_INTERP 1)
    else()
        set(WAMR_BUILD_INTERP 0)
    endif()

    if(CONFIG_WAMR_INTERP_FAST)
        set(WAMR_BUILD_FAST_INTERP 1)
    else()
        set(WAMR_BUILD_FAST_INTERP 0)
    endif()

    if(CONFIG_WAMR_INTERP_LOADER_MINI)
        set(WAMR_BUILD_MINI_LOADER 1)
    else()
        set(WAMR_BUILD_MINI_LOADER 0)
    endif()

    if(CONFIG_WAMR_ENABLE_AOT)
        set(WAMR_BUILD_AOT 1)
    else()
        set(WAMR_BUILD_AOT 0)
    endif()

    if(CONFIG_WAMR_ENABLE_LIBC_BUILTIN)
        set(WAMR_BUILD_LIBC_BUILTIN 1)
    else()
        set(WAMR_BUILD_LIBC_BUILTIN 0)
    endif()

    if(CONFIG_IDF_TARGET_ARCH_XTENSA)
        set (WAMR_BUILD_TARGET "XTENSA")
    else()
        if(CONFIG_IDF_TARGET_ARCH_RISCV)
            set (WAMR_BUILD_TARGET "RISCV32")
        endif()
    endif()

    if(CONFIG_WAMR_BUILD_DEBUG)
        set (CMAKE_BUILD_TYPE Debug)
    else()
        set (CMAKE_BUILD_TYPE Release)
    endif()

    if(CONFIG_WAMR_ENABLE_MULTI_MODULE)
        set (WAMR_BUILD_MULTI_MODULE 1)
    else()
        set (WAMR_BUILD_MULTI_MODULE 0)
    endif()

    if(CONFIG_WASM_ENABLE_SHARED_MEMORY)
        set (WASM_BUILD_SHARED_MEMORY 1)
    else()
        set (WASM_BUILD_SHARED_MEMORY 0)
    endif()

    if(CONFIG_WAMR_ENABLE_MEMORY_PROFILING)
        set (WAMR_BUILD_MEMORY_PROFILING 1)
    else()
        set (WAMR_BUILD_MEMORY_PROFILING 0)
    endif()

    if(CONFIG_WAMR_ENABLE_PERF_PROFILING)
        set (WAMR_BUILD_PERF_PROFILING 1)
    else()
        set (WAMR_BUILD_PERF_PROFILING 0)
    endif()

    if(CONFIG_WAMR_ENABLE_REF_TYPES)
        set (WAMR_BUILD_REF_TYPES 1)
    else()
        set (WAMR_BUILD_REF_TYPES 0)
    endif()

    if(CONFIG_WAMR_ENABLE_APP_FRAMEWORK)
        set (WAMR_BUILD_APP_FRAMEWORK 1)
        set (WAMR_BUILD_APP_LIST WAMR_APP_BUILD_BASE)
    else()
        set (WAMR_BUILD_APP_FRAMEWORK 0)
    endif()

    if(CONFIG_WAMR_ENABLE_LIB_PTHREAD)
        set (WAMR_BUILD_LIB_PTHREAD 1)
    else()
        set (WAMR_BUILD_LIB_PTHREAD 0)
    endif()

    include(${COMPONENT_PATH}/wasm-micro-runtime/build-scripts/runtime_lib.cmake)

    set(srcs "${WAMR_RUNTIME_LIB_SOURCE}"
             "${PLATFORM_SHARED_SOURCE}")

    set(include_dirs "${IWASM_DIR}/include"
                     "${UTILS_SHARED_DIR}"
                     "${PLATFORM_SHARED_DIR}"
                     "${PLATFORM_SHARED_DIR}/../include"
                     "${APP_MGR_SHARED_DIR}"
                     "${NATIVE_INTERFACE_DIR}")

    if(CONFIG_WAMR_IMPORT_ESP_LIBC_WASI)
        list(APPEND include_dirs "esp-libc-wasi/include")
        list(APPEND srcs "esp-libc-wasi/esp_libc_wasi_wrapper.c")
    endif()
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES pthread esp_timer)
