# nRF application makefile
PROJECT_NAME = $(shell basename "$(realpath ./)")

# Configurations
NRF_IC = nrf52840
SDK_VERSION = 15
SOFTDEVICE_MODEL = s140
USE_THREAD = 1

# Source and header files
APP_HEADER_PATHS += .
APP_SOURCE_PATHS += .
APP_SOURCES = $(notdir $(wildcard ./*.c))

# Path to base of nRF52-base repo
NRF_BASE_DIR = ../../buckler/software/nrf5x-base/

# Include board Makefile (if any)
include ../../buckler/software/boards/buckler_revA/Board.mk

# Include main Makefile
include $(NRF_BASE_DIR)make/AppMakefile.mk
