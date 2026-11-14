# SPDX-License-Identifier: Apache-2.0
CC ?= clang
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -fPIC -pthread \
          -Iinclude \
          -I../../kernel/include/uapi \
          -I../../kernel/drivers/accel/qpu

SRCS = src/libomniq_qpu.c \
       ../../kernel/drivers/accel/qpu/qpu_qmmu.c \
       ../../kernel/drivers/accel/qpu/qpu_fallback.c \
       ../../kernel/drivers/accel/qpu/qpu_ioctl.c

OBJS = $(SRCS:.c=.o)

LIB_A = libomniq_qpu.a

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LIB_SO = libomniq_qpu.dylib
    SO_FLAGS = -dynamiclib
else
    LIB_SO = libomniq_qpu.so
    SO_FLAGS = -shared -Wl,-soname,$(LIB_SO)
endif

all: $(LIB_A) $(LIB_SO)

$(LIB_A): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(LIB_SO): $(OBJS)
	$(CC) $(SO_FLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB_A) $(LIB_SO)

.PHONY: all clean
