# SPDX-License-Identifier: Apache-2.0
CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -pthread -I../kernel/include/uapi -I../kernel/drivers/accel/qpu

SRCS = test_qpu_uapi.c \
       ../kernel/drivers/accel/qpu/qpu_qmmu.c \
       ../kernel/drivers/accel/qpu/qpu_fallback.c \
       ../kernel/drivers/accel/qpu/qpu_ioctl.c

TARGET = test_qpu_uapi

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all test clean

