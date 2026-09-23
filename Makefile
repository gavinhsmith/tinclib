# Builds every example under examples/. Each one is an independent CE C
# Toolchain program compiled together with the library sources in src/; see
# project.mk for the actual build rules, and `make hello` to build just one.
#
# The hardware tests under tests/hw/ are built the same way (`make hw-build`)
# and run in CEmu's autotester with `make hw-test`; see tests/hw/run.py.

EXAMPLES := hello size_min size_full
HWTESTS := $(addprefix hw_,$(notdir $(patsubst %/,%,$(dir $(wildcard tests/hw/*/autotest.json))))) hw_stub_tinclibc

PYTHON ?= python

.PHONY: all clean pc-link hw-build hw-test hw-record size-check $(EXAMPLES) $(HWTESTS)

all: $(EXAMPLES)

$(EXAMPLES) $(HWTESTS):
	$(MAKE) -f project.mk EXAMPLE=$@

hw-build: $(HWTESTS)

# Builds and runs the hardware tests. Needs AUTOTESTER_ROM; HW_ARGS is passed
# to run.py, e.g. `make hw-test HW_ARGS="nodevice handoff"`.
hw-test:
	$(PYTHON) tests/hw/run.py $(HW_ARGS)

# Re-records the expected screen CRCs of failing hashes (review the PNGs!).
hw-record:
	$(PYTHON) tests/hw/run.py --record $(HW_ARGS)

# Checks the linker drops what a program doesn't call: size_min only uses
# tinc_init/tinc_isActive, size_full is the same program using everything.
size-check: size_min size_full
	$(PYTHON) tools/size_check.py bin/size_min/TINCSIZE.8xp bin/size_full/TINCFULL.8xp

# The library on a Windows PC against a real board on a COM port (see
# tools/pc_link.c). Needs a host gcc (MSYS2 on Windows, run from PowerShell).
HOSTCC ?= gcc
PROTO := external/tinclib-protocol

pc-link:
	$(PYTHON) -c "import os; os.makedirs('bin', exist_ok=True)"
	$(HOSTCC) -std=c99 -Wall -Wextra -O1 -Itests/stubs -Isrc -I$(PROTO) -o bin/pc_link.exe \
		tools/pc_link.c $(wildcard src/*.c) $(PROTO)/crc16.c $(PROTO)/tinc_frame.c

# Removes all build output. Done here rather than with CEdev's clean, whose
# Windows version silently fails on paths with '/' in them.
clean:
	$(PYTHON) -c "import shutil; [shutil.rmtree(d, ignore_errors=True) for d in ('bin', 'obj', 'tests/hw/build')]"
