cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Contournement d'un bug du pilote ADXL345 (SPI) dans Zephyr 3.7.99 : TRUE non defini
zephyr_compile_definitions(TRUE=1)

project(predictmotor)
target_sources(app PRIVATE src/main.c)
project(predictmotor)
target_sources(app PRIVATE src/main.c)
