# Build one target:  make [CC=... AR=... STATIC=0 EXTRA_CFLAGS=... OUT=build/name]
# (build.py drives this for the cross/NDK/musl targets.)
CC ?= cc
AR ?= ar
STATIC ?= 1
OUT ?= build/native
EXTRA_CFLAGS ?=

CFLAGS += -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude -Isrc $(EXTRA_CFLAGS)
LDFLAGS += $(if $(filter 1,$(STATIC)),-static,-pie)

LIB_SRCS = emuroot.c path.c ptrace.c envonly.c
LIB_OBJS = $(addprefix $(OUT)/obj/,$(LIB_SRCS:.c=.o))

all: $(OUT)/bin/emuroot $(OUT)/lib/libemuroot.a

$(OUT)/obj/%.o: src/%.c src/internal.h src/arch.h include/emuroot.h
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
