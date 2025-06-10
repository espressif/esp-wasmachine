set(srcs "wm_main.c")

idf_component_register(SRCS "${srcs}")

# Create a SPIFFS image from the contents of the 'spiffs_image' directory
# that fits the partition named 'storage'. It's not friendly to flash the 
# generated image with 'idf.py -p PORT flash', since the image will be
# flashed each time, but you can use 'idf.py -p PORT storage-flash' to
# flash this image separately.
littlefs_create_partition_image(storage fs_image)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/lvgl__lvgl)
    # Apply patches to managed components
    set(PATCH_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../tools/apply_patch.py")
    set(LVGL_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/../tools/patches/lvgl.patch")
    set(LVGL_TARGET "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/lvgl__lvgl")

    add_custom_target(
        patch_components ALL
        COMMAND ${CMAKE_COMMAND} -E echo "=== Applying component patches ==="
        COMMAND python3 "${PATCH_SCRIPT}" --patch "${LVGL_PATCH}" --target "${LVGL_TARGET}"
        COMMENT "Applying patches to managed components"
        VERBATIM
    )

    add_dependencies(${COMPONENT_LIB} patch_components)

    idf_component_get_property(lvgl lvgl__lvgl COMPONENT_LIB)
    target_compile_definitions(${lvgl} PUBLIC "-DLV_EXTERNAL_DATA_AND_DESTRUCTOR"
                                            "-DLV_EXT_DATA_MAX_NUM=3")
endif()