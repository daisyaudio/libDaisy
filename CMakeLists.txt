cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0135 NEW)

# Set up our toolchain for cross-compilation if that's happening
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/toolchains/autodetect.cmake)
endif()

# Declare our project
project(libDaisy VERSION 7.0.1)

# Append our /cmake to the module path
# This allows us to use DaisyProject as `include(DaisyProject)`
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

if(NOT PROJECT_IS_TOP_LEVEL)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} PARENT_SCOPE)
else()
    # Need to set sane default compile options for standalone library build
    add_compile_options(
        $<$<CONFIG:Debug,RelWithDebInfo>:-ggdb3>
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:MinSizeRel>:-Os>
    )
    add_compile_options($<$<CONFIG:Release,MinSizeRel,RelWithDebInfo>:-flto>)
    add_link_options($<$<CONFIG:Release,MinSizeRel,RelWithDebInfo>:-flto>)

    # Define NDEBUG when not compiling with debug flags
    add_compile_definitions($<$<CONFIG:Release,MinSizeRel>:NDEBUG>)
endif()

cmake_path(SET MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/src")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/internal/tools.cmake)

add_subdirectory(Drivers)
add_subdirectory(Middlewares/ST)
add_subdirectory(Middlewares/Third_Party/FatFs)

add_library(daisy STATIC
    ${MODULE_DIR}/daisy_field.cpp
    ${MODULE_DIR}/daisy_legio.cpp
    ${MODULE_DIR}/daisy_patch_sm.cpp
    ${MODULE_DIR}/daisy_patch.cpp
    ${MODULE_DIR}/daisy_petal.cpp
    ${MODULE_DIR}/daisy_pod.cpp
    ${MODULE_DIR}/daisy_seed.cpp
    ${MODULE_DIR}/daisy_versio.cpp
    ${MODULE_DIR}/desktop_devkit.cpp
    ${MODULE_DIR}/eurorack_devkit.cpp
    ${MODULE_DIR}/pedal_devkit.cpp
    ${MODULE_DIR}/dev/codec_ak4556.cpp
    ${MODULE_DIR}/dev/codec_pcm3060.cpp
    ${MODULE_DIR}/dev/codec_wm8731.cpp
    ${MODULE_DIR}/dev/lcd_hd44780.cpp
    ${MODULE_DIR}/dev/sdram.cpp
    ${MODULE_DIR}/dev/sr_595.cpp
    ${MODULE_DIR}/dev/trill/Trill.cpp
    ${MODULE_DIR}/dev/trill/CentroidDetection.cpp
    ${MODULE_DIR}/hid/audio.cpp
    ${MODULE_DIR}/hid/ctrl.cpp
    ${MODULE_DIR}/hid/encoder.cpp
    ${MODULE_DIR}/hid/gatein.cpp
    ${MODULE_DIR}/hid/led.cpp
    ${MODULE_DIR}/hid/logger.cpp
    ${MODULE_DIR}/hid/midi_parser.cpp
    ${MODULE_DIR}/hid/midi.cpp
    ${MODULE_DIR}/hid/parameter.cpp
    ${MODULE_DIR}/hid/rgb_led.cpp
    ${MODULE_DIR}/hid/switch.cpp
    ${MODULE_DIR}/hid/usb_host.cpp
    ${MODULE_DIR}/hid/usb_midi.cpp
    ${MODULE_DIR}/hid/usb.cpp
    ${MODULE_DIR}/per/adc.cpp
    ${MODULE_DIR}/per/dac.cpp
    ${MODULE_DIR}/per/gpio.cpp
    ${MODULE_DIR}/per/i2c.cpp
    ${MODULE_DIR}/per/qspi.cpp
    ${MODULE_DIR}/per/rng.cpp
    ${MODULE_DIR}/per/sai.cpp
    ${MODULE_DIR}/per/sdmmc.cpp
    ${MODULE_DIR}/per/spi.cpp
    ${MODULE_DIR}/per/spiMultislave.cpp
    ${MODULE_DIR}/per/tim.cpp
    ${MODULE_DIR}/per/uart.cpp
    ${MODULE_DIR}/sys/dma.c
    ${MODULE_DIR}/sys/fatfs.cpp
    ${MODULE_DIR}/sys/system.cpp
    ${MODULE_DIR}/ui/AbstractMenu.cpp
    ${MODULE_DIR}/ui/FullScreenItemMenu.cpp
    ${MODULE_DIR}/ui/UI.cpp
    ${MODULE_DIR}/usbd/usbd_cdc_if.c
    ${MODULE_DIR}/usbd/usbd_conf.c
    ${MODULE_DIR}/usbd/usbd_desc.c
    ${MODULE_DIR}/usbh/usbh_conf.c
    ${MODULE_DIR}/util/bsp_sd_diskio.c
    ${MODULE_DIR}/util/color.cpp
    ${MODULE_DIR}/util/MappedValue.cpp
    ${MODULE_DIR}/util/oled_fonts.c
    ${MODULE_DIR}/util/sd_diskio.c
    ${MODULE_DIR}/util/unique_id.c
    ${MODULE_DIR}/util/usbh_diskio.c
    ${MODULE_DIR}/util/WaveTableLoader.cpp
    core/startup_stm32h750xx.c
)

target_include_directories(daisy PUBLIC
    ${MODULE_DIR}
)

target_link_libraries(daisy PUBLIC
    CMSIS_CORE
    CMSIS_DEVICE_H7
    STM32H7XX_HAL_DRIVER
    STM32_USB_DEVICE_LIBRARY
    STM32_USB_HOST_LIBRARY
    FatFs
)

# There is a abstract `IReader` interface that allows that can be swappped out
# for other i/o, but libDaisy assumes FatFS will always be present.
# It may make sense for this to move to better facillitate testing, etc.
target_compile_definitions(daisy PUBLIC FILEIO_ENABLE_FATFS_READER)

target_compile_options(daisy PRIVATE
    -Wall

    # At same point this generator should be removed, but the clang build isn't there yet
    $<$<CXX_COMPILER_ID:GNU>:-Werror>

    # These are explicitly for startup_stm32h750xx.c
    $<$<CXX_COMPILER_ID:GNU>:-Wno-attributes>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-missing-attributes>
)

set_target_properties(daisy PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED
    C_STANDARD 11
    C_STANDARD_REQUIRED
)

# Don't add examples if we're being included in another project
if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(examples EXCLUDE_FROM_ALL)
    get_all_targets(example_targets examples)
    add_custom_target(examples DEPENDS ${example_targets})
endif()
