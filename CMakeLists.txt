cmake_minimum_required(VERSION 3.20)
project(S800_Smart_Clock)

# MCU firmware — CLion code-intelligence only, not a real build.
# Actual build uses Keil MDK-ARM: UV4.exe -r mcu/main.uvprojx -t "Target 1"
add_subdirectory(mcu)
