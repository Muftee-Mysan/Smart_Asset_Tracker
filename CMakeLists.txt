cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME asset_tracker)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    App/app_main.c
    App/app_sm.c
    App/crc.c
    App/ui.c
    App/data/students_table.c
    App/bringup.c
    App/fmt.c
    App/drivers/delay.c
    App/drivers/font.c
    App/drivers/fram.c
    App/drivers/i2c_bus.c
    App/drivers/keypad.c
    App/drivers/led.c
    App/drivers/pn532.c
    App/drivers/ssd1306.c
    App/drivers/watchdog.c
    App/services/battery.c
    App/services/export.c
    App/services/storage.c
    App/services/students.c
    App/services/power.c
    App/services/rtc_time.c
    App/services/trace.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    App
    App/drivers
    App/services
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    $<$<CONFIG:Debug>:APP_DEBUG=1>
)

# Student table: regenerate App/data/students_table.c whenever the CSV or the
# generator changes (CLAUDE.md 6.11).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/App/data/students_table.c
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_students.py
            ${CMAKE_SOURCE_DIR}/tools/students.csv
            ${CMAKE_SOURCE_DIR}/App/data/students_table.c
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/students.csv ${CMAKE_SOURCE_DIR}/tools/gen_students.py
    COMMENT "Generating students_table.c from tools/students.csv"
    VERBATIM
)

# Extra warnings for Core/ and App/ sources only. The ST HAL (STM32_Drivers
# object library) is left at the generated -Wall: -Wextra flags unused
# parameters in stm32f4xx_hal_flash_ex.c that we must not edit.
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wextra)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
