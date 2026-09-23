# Host-side unit tests: builds the library with the native C compiler against
# the stand-in CE headers in stubs/ and a fake board, and runs the suite.
#
#     make -C tests          # build and run
#     make -C tests CC=clang # any C99 compiler works
#
# Sanitizers are on by default; SANITIZE= turns them off (e.g. for compilers
# or platforms without ASan/UBSan).

CC ?= cc
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CFLAGS ?= -std=c99 -g -O1 -Wall -Wextra -Werror
PROTO := ../external/tinclib-protocol
# clock() is the fake one in stubs.c, so timeouts run instantly.
CPPFLAGS += -Istubs -I../src -I$(PROTO) -I$(PROTO)/test_vectors -Dclock=tinc_fake_clock

BUILD := build
TEST := $(BUILD)/test_tinclib

SOURCES := test_tinclib.c stubs/stubs.c $(wildcard ../src/*.c) $(PROTO)/crc16.c $(PROTO)/tinc_frame.c
DEPS := $(SOURCES) $(wildcard ../src/*.h $(PROTO)/*.h stubs/*.h stubs/ti/*.h)

.PHONY: all test clean

all: test

test: $(TEST)
	./$(TEST)

$(TEST): $(DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) -o $@ $(SOURCES)

clean:
	rm -rf $(BUILD)
