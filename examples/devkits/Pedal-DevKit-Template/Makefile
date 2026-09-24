# Project Name
TARGET = Pedal-DevKit-Template

# Configure for debugging
# common configurations:
# use DEBUG = 1 and OPT = -Og for debugging
# or DEBUG = 0 and OPT = -O3 for performance
DEBUG = 1
OPT = -Og

#(optional) use the bootloader
# APP_TYPE=BOOT_SRAM

# Sources
CPP_SOURCES = src/main.cpp

LIBDAISY_DIR = ../../../

# (optional) Add DaisySP
# DAISYSP_DIR = ../path/to/DaisySP
# USE_DAISYSP_LGPL=1

# (optional) Includes FatFS source files within project.
USE_FATFS = 1

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

include $(SYSTEM_FILES_DIR)/Makefile
