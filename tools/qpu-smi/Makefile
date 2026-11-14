# SPDX-License-Identifier: Apache-2.0
CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -pthread \
          -I../../runtime/libomniq-qpu/include \
          -I../../kernel/include/uapi

LDFLAGS ?= ../../runtime/libomniq-qpu/libomniq_qpu.a -lpthread

TARGET = qpu-smi

all: $(TARGET)

$(TARGET): main.c ../../runtime/libomniq-qpu/libomniq_qpu.a
	$(CC) $(CFLAGS) -o $@ main.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
