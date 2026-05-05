CXX = g++
# Use relative paths for portability
H3PPCI_INC_DIR = ./include
H3PPCI_LIB_DIR = ./lib

# CUDA path discovery
# Priority: 1. Environment variable CUDA_PATH, 2. /usr/local/cuda, 3. Results from 'which nvcc'
CUDA_PATH ?= $(firstword $(wildcard /usr/local/cuda) $(shell which nvcc >/dev/null 2>&1 && readlink -f $$(which nvcc) | sed 's|/bin/nvcc||'))

# NVTX Path (Nsight Systems usually provides nvToolsExtCounters.h)
NVTX_PATH ?= /opt/nvidia/nsight-systems/2026.1.1/target-linux-x64/nvtx/include

# Robust include path detection by searching for the actual NVTX header
POSSIBLE_HEADERS = $(NVTX_PATH)/nvtx3/nvToolsExtCounters.h \
                   $(CUDA_PATH)/targets/x86_64-linux/include/nvtx3/nvToolsExtCounters.h \
                   $(CUDA_PATH)/include/nvtx3/nvToolsExtCounters.h \
                   /usr/include/nvtx3/nvToolsExtCounters.h \
                   /usr/local/cuda/include/nvtx3/nvToolsExtCounters.h

FOUND_HEADER := $(firstword $(foreach h,$(POSSIBLE_HEADERS),$(wildcard $(h))))
CUDA_INC_DIR := $(patsubst %/nvtx3/nvToolsExtCounters.h,%,$(FOUND_HEADER))

# Fallback if detection fails
ifeq ($(CUDA_INC_DIR),)
    CUDA_INC_DIR := /usr/local/cuda/include
endif

CXXFLAGS = -I$(H3PPCI_INC_DIR) -I$(CUDA_INC_DIR) -I$(NVTX_PATH) -Wall
# Use static linking for libh3ppci to avoid runtime dynamic linker missing symbol errors
LDFLAGS = $(H3PPCI_LIB_DIR)/libh3ppci.a -ldl -pthread
TARGET = h3_sw_counters
SRCS = src/sw_nsys_plugin.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
