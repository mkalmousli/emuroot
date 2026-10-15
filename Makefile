# Build one target:  make [CC=... AR=... STATIC=0 PTRACE=0 EXTRA_CFLAGS=... OUT=build/name]
# (build.py drives this for the cross/NDK/musl targets.)
# PTRACE=0 omits the Linux ptrace engine and builds only the portable env-only
# backend; it is the default on anything but Linux.
CC ?= cc
AR ?= ar
STATIC ?= 1
OUT ?= build/native
EXTRA_CFLAGS ?=
PTRACE ?= $(if $(filter Linux,$(shell uname -s)),1,0)

CFLAGS += -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude -Isrc $(EXTRA_CFLAGS)
LDFLAGS += $(if $(filter 1,$(STATIC)),-static,$(if $(filter Linux,$(shell uname -s)),-pie,))

LIB_SRCS = emuroot.c path.c envonly.c
ifeq ($(PTRACE),1)
LIB_SRCS += exec.c creds.c seccomp.c syscalls.c rewrite.c fake.c ptrace.c
else
CFLAGS += -DEMUROOT_NO_PTRACE
endif
LIB_OBJS = $(addprefix $(OUT)/obj/,$(LIB_SRCS:.c=.o))

all: $(OUT)/bin/emuroot $(OUT)/lib/libemuroot.a

$(OUT)/obj/%.o: src/%.c src/internal.h src/arch.h src/guest.h src/syscalls.h src/tracee.h include/emuroot.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/lib/libemuroot.a: $(LIB_OBJS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(OUT)/bin/emuroot: $(OUT)/obj/main.o $(OUT)/lib/libemuroot.a
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -L$(OUT)/lib -lemuroot -o $@

clean:
	rm -rf build

.PHONY: all clean
