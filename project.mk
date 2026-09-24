# CE C Toolchain project for one example program or hardware test. Not meant
# to be run directly: the root Makefile invokes this once per program, e.g.
#
#     make -f project.mk EXAMPLE=hello
#     make -f project.mk EXAMPLE=hw_nodevice   # tests/hw/nodevice/main.c
#
# The library in src/ and the protocol sources from the tinclib-protocol
# submodule are compiled straight into each program. The project lives at
# the repo root so no source path needs a `..` -- the toolchain cannot build
# those on Windows (it maps `..` to a directory named `_..`, and Windows
# drops the trailing dots).

EXAMPLE ?= hello

PROTO = external/tinclib-protocol

# Program names for the hardware tests; each must match the "target" name in
# tests/hw/<test>/autotest.json, and sort before TINCLIBC: run.py's OS 5.5+
# launch picks the first program with the same first letter. stub_tinclibc
# stands in for the config app.
HW_NAME_canary = TNCANARY
HW_NAME_nodevice = TNNODEV
HW_NAME_handoff = TAHANDOF
HW_NAME_stub_tinclibc = TINCLIBC

ifeq ($(EXAMPLE),hello)
NAME = TINCHELO
DESCRIPTION = "tinclib hello"
else ifeq ($(EXAMPLE),size_min)
NAME = TINCSIZE
DESCRIPTION = "tinclib size check"
else ifeq ($(EXAMPLE),size_full)
NAME = TINCFULL
DESCRIPTION = "tinclib size check"
MAIN = examples/size_min/main.c
SIZE_FLAGS = -DTINC_SIZE_FULL
else ifeq ($(EXAMPLE),size_cpp)
NAME = TINCCPP
DESCRIPTION = "tinclib size check"
MAIN_CPP = examples/size_min/main.cpp
else ifneq ($(HW_NAME_$(EXAMPLE:hw_%=%)),)
NAME = $(HW_NAME_$(EXAMPLE:hw_%=%))
DESCRIPTION = "tinclib hw test"
MAIN = tests/hw/$(EXAMPLE:hw_%=%)/main.c
else
$(error unknown EXAMPLE '$(EXAMPLE)': expected a directory under examples/ or tests/hw/ listed here)
endif

ifndef MAIN_CPP
MAIN ?= examples/$(EXAMPLE)/main.c
endif

ICON =
COMPRESSED = NO

CFLAGS = -Wall -Wextra -Oz -Isrc -I$(PROTO) $(SIZE_FLAGS)
CXXFLAGS = -Wall -Wextra -Oz -Isrc -I$(PROTO)

# src/*.c (the library) is picked up automatically as SRCDIR; add the
# protocol and the program.
EXTRA_C_SOURCES = $(MAIN) $(PROTO)/crc16.c $(PROTO)/tinc_frame.c
EXTRA_CXX_SOURCES = $(MAIN_CPP)
EXTRA_HEADERS = $(wildcard src/*.h src/*.hpp) $(wildcard $(PROTO)/*.h)

OBJDIR = obj/$(EXAMPLE)
BINDIR = bin/$(EXAMPLE)

include $(shell cedev-config --makefile)
