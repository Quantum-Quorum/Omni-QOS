# SPDX-License-Identifier: Apache-2.0
CC ?= gcc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -fPIC -pthread \
          -Iinclude \
          -I../../kernel/include/uapi \
          -I../../kernel/drivers/accel/qpu

BUILD_DIR = build
OBJS = $(BUILD_DIR)/libomniq_qpu.o \
       $(BUILD_DIR)/qpu_dma.o \
       $(BUILD_DIR)/qpu_qmmu.o \
       $(BUILD_DIR)/qpu_fallback.o \
       $(BUILD_DIR)/qpu_ioctl.o

LIB_A = libomniq_qpu.a

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LIB_SO = libomniq_qpu.dylib
    LIB_ALIAS = libomniq-qpu.dylib
    SO_FLAGS = -dynamiclib
else
    LIB_SO = libomniq_qpu.so
    LIB_ALIAS = libomniq-qpu.so
    SO_FLAGS = -shared -Wl,-soname,$(LIB_SO) -lpthread
endif

all: $(LIB_A) $(LIB_SO) $(LIB_ALIAS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libomniq_qpu.o: src/libomniq_qpu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qpu_dma.o: ../../kernel/drivers/accel/qpu/qpu_dma.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qpu_qmmu.o: ../../kernel/drivers/accel/qpu/qpu_qmmu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qpu_fallback.o: ../../kernel/drivers/accel/qpu/qpu_fallback.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qpu_ioctl.o: ../../kernel/drivers/accel/qpu/qpu_ioctl.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_A): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(LIB_SO): $(OBJS)
	$(CC) $(SO_FLAGS) -o $@ $(OBJS)

$(LIB_ALIAS): $(LIB_SO)
	ln -sf $(LIB_SO) $(LIB_ALIAS)

clean:
	rm -rf $(BUILD_DIR) $(LIB_A) $(LIB_SO) $(LIB_ALIAS)

.PHONY: all clean
