cmake_minimum_required(VERSION 3.20)
project(main C)

# ARM Compiler 5 built-in headers would normally come from Keil's toolchain.
# This simulates them so CLion can resolve <stdint.h> etc. when armcc is not installed.
add_compile_definitions(
    __CC_ARM
    TM4C1294NCPDT
    PART_TM4C1294NCPDT
    _RTE_
)

include_directories(
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/inc"
    "${CMAKE_SOURCE_DIR}/driverlib"
    "${CMAKE_SOURCE_DIR}/RTE/_Target_1"
    "${CMAKE_SOURCE_DIR}/User/base"
    "${CMAKE_SOURCE_DIR}/User/app"
)

add_executable(${PROJECT_NAME}
    User/main.c
    User/callback.c
    User/base/board.c
    User/base/delay.c
    User/base/hibernate_rtc.c
    User/base/i2c_bus.c
    User/base/pwm_buzzer.c
    User/base/stepper.c
    User/base/timebase.c
    User/base/uart_port.c
    User/app/alarm.c
    User/app/app.c
    User/app/app_state.c
    User/app/boot_sequence.c
    User/app/button_logic.c
    User/app/command.c
    User/app/datetime.c
    User/app/display.c
    User/app/melody.c
    RTE/Device/TM4C1294NCPDT/system_TM4C129.c
    RTE/Device/TM4C1294NCPDT/startup_TM4C129.s
)
