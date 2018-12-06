#nRF application makefile
PROJECT_NAME = $(shell basename "$(realpath ./)")

# Configurations
NRF_IC = nrf52840
SDK_VERSION = 15
<<<<<<< HEAD
SOFTDEVICE_MODEL = s140
USE_THREAD = 0
=======
SOFTDEVICE_MODEL = blank
USE_THREAD=1
>>>>>>> 21ac2f5be8de7c710ea1d8a293bf11ce313e6a55

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
