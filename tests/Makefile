# Host-side unit tests: builds the library with the native C compiler against
# the stand-in CE headers in stubs/ and a fake board, and runs the suite.
#
#     make -C tests          # build and run
#     make -C tests CC=clang CXX=clang++ # any C99 / C++11 compilers work
#
# Sanitizers are on by default; SANITIZE= turns them off (e.g. for compilers
# or platforms without ASan/UBSan).

CC ?= cc
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CFLAGS ?= -std=c99 -g -O1 -Wall -Wextra -Werror
# No exceptions or RTTI, as on the calculator.
CXXFLAGS ?= -std=c++11 -g -O1 -Wall -Wextra -Werror -fno-exceptions -fno-rtti
PROTO := ../external/tinclib-protocol
# clock() is the fake one in stubs.c, so timeouts run instantly.
CPPFLAGS += -Istubs -I../src -I$(PROTO) -I$(PROTO)/test_vectors -Dclock=tinc_fake_clock

BUILD := build
TEST := $(BUILD)/test_tinclib
TEST_CPP := $(BUILD)/test_cpp

LIB := stubs/stubs.c $(wildcard ../src/*.c) $(PROTO)/crc16.c $(PROTO)/tinc_frame.c
SOURCES := test_tinclib.c $(LIB)
DEPS := $(LIB) $(wildcard ../src/*.h ../src/*.hpp $(PROTO)/*.h stubs/*.h stubs/ti/*.h)

.PHONY: all test clean

all: test

test: $(TEST) $(TEST_CPP)
	./$(TEST)
	./$(TEST_CPP)

$(TEST): test_tinclib.c $(DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) -o $@ $(SOURCES)

# The C++ bindings: test_cpp.cpp as C++ against the library built as C.
LIB_OBJ := $(addprefix $(BUILD)/lib/,$(notdir $(LIB:.c=.o)))
vpath %.c stubs ../src $(PROTO)

$(BUILD)/lib/%.o: %.c $(DEPS)
	@mkdir -p $(BUILD)/lib
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) -c -o $@ $<

$(TEST_CPP): test_cpp.cpp $(LIB_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SANITIZE) -o $@ test_cpp.cpp $(LIB_OBJ)

clean:
	rm -rf $(BUILD)
