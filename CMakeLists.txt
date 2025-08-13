# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(VFO C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_subdirectory(external/pico-ssd1306)
add_subdirectory(external/pico-extras/src/common/pico_util_buffer)
add_subdirectory(external/pico-extras/src/common/pico_audio)
add_subdirectory(external/pico-extras/src/rp2_common/pico_audio_i2s)

add_executable(${PROJECT_NAME}
    main.cpp
    audio.cpp
    audio.h
    external/si5351/si5351.c
)

# pull in common dependencies and additional i2c hardware support
target_link_libraries(${PROJECT_NAME} pico_ssd1306 pico_stdlib hardware_i2c pico_audio_i2s)

target_include_directories(${PROJECT_NAME}
 PUBLIC 
    external
    external/pico-ssd1306/include
    external/si5351)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    # compile time configuration of I2S
    PICO_AUDIO_I2S_MONO_INPUT=1
    USE_AUDIO_I2S=1
    PICO_AUDIO_I2S_DATA_PIN=15
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=13
    )

# add url via pico_set_program_url
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
