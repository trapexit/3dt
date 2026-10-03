FILENAME := 3dt

ifdef TARGET
  EXE := $(FILENAME)_$(TARGET)
else
  EXE := $(FILENAME)
endif

JOBS := $(shell nproc)

OUTPUT = build/$(EXE)

.DEFAULT_GOAL := all

CC    ?= gcc
CXX   ?= g++
STRIP ?= strip
PYTHON ?= python3
ZIG_VENV ?= .venv
SYSTEM_ZIG := $(shell command -v zig 2>/dev/null)
ZIG ?= $(if $(SYSTEM_ZIG),$(SYSTEM_ZIG),$(abspath $(ZIG_VENV))/bin/python-zig)

ifeq ($(NDEBUG),1)
OPT := -Os -flto -ffunction-sections -fdata-sections
ifeq ($(filter %-macos,$(TARGET)),)
OPT += -static
LDFLAGS += -Wl,--gc-sections -Wl,--strip-all
else
LDFLAGS += -Wl,-dead_strip -Wl,-S -Wl,-x
endif
else
OPT := -O0 -ggdb -ftrapv
endif

ifeq ($(SANITIZE),1)
OPT += -fsanitize=address,undefined
endif

VENDORED_DIRS := vendored/ $(wildcard vendored/*/)
VENDORED_FLAGS := $(addprefix -I, $(VENDORED_DIRS))

CFLAGS = $(OPT) -Wall -Wextra -Wpedantic -Wshadow -Wno-error=date-time $(VENDORED_FLAGS)
CXXFLAGS = $(OPT) -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -std=c++17 $(VENDORED_FLAGS)
CPPFLAGS ?= -MMD -MP
VENDORED_CFLAGS = $(CFLAGS) \
	-Wno-\#pragma-messages \
	-Wno-date-time \
	-Wno-ignored-qualifiers \
	-Wno-invalid-utf8 \
	-Wno-shift-negative-value \
	-Wno-sign-compare \
	-Wno-strict-prototypes \
	-Wno-type-limits \
	-Wno-unused-parameter

SRCS_C   := $(wildcard src/*.c)
VENDORED_C := $(wildcard vendored/*/*.c)
SRCS_CXX := $(wildcard src/*.cpp)

ifdef TARGET
  BUILDDIR = build/$(TARGET)
else
  BUILDDIR = build
endif
OBJS := $(SRCS_C:src/%.c=$(BUILDDIR)/%.c.o)
OBJS += $(VENDORED_C:vendored/%.c=$(BUILDDIR)/vendored/%.c.o)
OBJS += $(SRCS_CXX:src/%.cpp=$(BUILDDIR)/%.cpp.o)
DEPS  = $(OBJS:.o=.d)


.PHONY: help
help:
	@echo "3dt - 3DO Disc Tool build system"
	@echo ""
	@echo "Usage: make [TARGET] [VARIABLE=VALUE]"
	@echo ""
	@echo "Targets:"
	@echo "  all       (default) Build debug binary"
	@echo "  clean     Remove build/ directory"
	@echo "  distclean Remove everything not in git"
	@echo "  strip     Strip debug symbols from binary"
	@echo "  zig-venv  Use Zig from PATH or install Zig in .venv"
	@echo "  release   Cross-compile release binaries with Zig"
	@echo "  help      Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  NDEBUG=1      Release build optimized for size"
	@echo "  SANITIZE=1    Add -fsanitize=address,undefined"
	@echo ""
	@echo "Cross-compile:"
	@echo "  make zig-venv             Provision Zig if not already on PATH"
	@echo "  make release              Build all release targets with Zig"
	@echo "  make TARGET=<zig-target>  Build one named output with custom CC/CXX"
	@echo ""
	@echo "GitHub release (commit all changes first):"
	@echo "  tools/release-to-github             Build, tag, and upload a draft"
	@echo "  tools/release-to-github --publish   Also publish as latest"
	@echo ""
	@echo "Output: $(OUTPUT)"

all: $(OUTPUT)

$(OUTPUT): $(OBJS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $(OUTPUT) $(OBJS) $(LDFLAGS)

$(OBJS): | $(BUILDDIR)

strip: $(OUTPUT)
	$(STRIP) --strip-all $(OUTPUT)

$(BUILDDIR)/%.c.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/vendored/%.c.o: vendored/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(VENDORED_CFLAGS) -c $< -o $@

$(BUILDDIR)/%.cpp.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rfv build/

distclean: clean
	git clean -fdx

$(BUILDDIR):
	mkdir -p $@

PREFIX ?= $(HOME)/dev/3do-devkit
BINDIR ?= $(PREFIX)/bin/tools/linux

install: $(OUTPUT)
	install -Dm755 $(OUTPUT) $(DESTDIR)$(BINDIR)/$(EXE)

zig-venv:
ifneq ($(SYSTEM_ZIG),)
	@echo "Using system Zig: $(SYSTEM_ZIG)"
else
	$(PYTHON) -m venv "$(ZIG_VENV)"
	"$(ZIG_VENV)/bin/python" -m pip install "ziglang==0.16.0"
endif

release:
	@"$(ZIG)" version >/dev/null 2>&1 || { \
		echo "Zig not found; run 'make zig-venv' first." >&2; \
		exit 1; \
	}
	$(MAKE) clean
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="$(ZIG) cc -target x86_64-linux-musl" \
		CXX="$(ZIG) c++ -target x86_64-linux-musl" \
		STRIP="$(ZIG) llvm-strip" \
		TARGET="x86_64-linux-musl" \
		OPT="-Oz -flto -ffunction-sections -fdata-sections -static"
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="$(ZIG) cc -target aarch64-linux-musl" \
		CXX="$(ZIG) c++ -target aarch64-linux-musl" \
		STRIP="$(ZIG) llvm-strip" \
		TARGET="aarch64-linux-musl" \
		OPT="-Oz -flto -ffunction-sections -fdata-sections -static"
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="$(ZIG) cc -target x86_64-windows-gnu" \
		CXX="$(ZIG) c++ -target x86_64-windows-gnu" \
		STRIP="$(ZIG) llvm-strip" \
		TARGET="x86_64-windows-gnu.exe" \
		OPT="-Oz -ffunction-sections -fdata-sections -static"
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="$(ZIG) cc -target aarch64-macos" \
		CXX="$(ZIG) c++ -target aarch64-macos" \
		STRIP="$(ZIG) llvm-strip" \
		TARGET="aarch64-macos" \
		OPT="-Oz -ffunction-sections -fdata-sections"

.PHONY: all clean distclean release zig-venv strip install

-include $(DEPS)
