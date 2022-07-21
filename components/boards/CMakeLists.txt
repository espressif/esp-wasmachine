
set(src_dirs "${BOARD}/src"
             "common/codec/es7210"
             "common/codec/es7243e"
             "common/codec/es8156"
             "common/codec/es8311"
             "common/codec/es8388"
             "common/i2c_bus"
             "common/i2c_devices/icm42670"
             "common/i2c_devices/touch_panel"
             "common/iot_button"
             "common/peripherals")

set(include_dirs "${BOARD}/src"
                 "common/include"
                 "common/codec/es7210"
                 "common/codec/es7243e"
                 "common/codec/es8156"
                 "common/codec/es8311"
                 "common/codec/es8388"
                 "common/codec/include"
                 "common/i2c_bus/include"
                 "common/i2c_devices/icm42670/include"
                 "common/i2c_devices/touch_panel/include"
                 "common/iot_button/include"
                 "common/peripherals/include")

idf_component_register(SRC_DIRS ${src_dirs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES "esp_hid" "esp_lcd" "esp_adc_cal")

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-incompatible-pointer-types)
