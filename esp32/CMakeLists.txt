# fish-fodder for an ESP32-S3 e-paper frame (ESP-IDF 5.3 or later).
#   idf.py set-target esp32s3 && idf.py menuconfig && idf.py flash monitor
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fish-fodder)
