FILENAME := 3dt

ifdef TARGET
  EXE := $(FILENAME)_$(TARGET)
else
  EXE := $(FILENAME)
endif

JOBS := $(shell nproc)
PUID := $(shell id -u)
PGID := $(shell id -g)

OUTPUT = build/$(EXE)

.DEFAULT_GOAL := all

CC    ?= gcc
CXX   ?= g++
STRIP ?= strip

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
	@echo "  release   Build containerized release binaries"
	@echo "  help      Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  NDEBUG=1      Release build optimized for size"
	@echo "  SANITIZE=1    Add -fsanitize=address,undefined"
	@echo ""
	@echo "Cross-compile:"
	@echo "  make release              Build all release targets via Podman/Zig"
	@echo "  make release-base         Build all release targets with local Zig"
	@echo "  make TARGET=<zig-target>  Build one named output with custom CC/CXX"
	@echo ""
	@echo "Output: $(OUTPUT)"

all: $(OUTPUT)

$(OUTPUT): builddir $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(OUTPUT) $(OBJS) $(LDFLAGS)

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

builddir:
	mkdir -p $(BUILDDIR)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(OUTPUT)
	install -Dm755 $(OUTPUT) $(DESTDIR)$(BINDIR)/$(EXE)

release-base: clean
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="zig cc -target x86_64-linux-musl" \
		CXX="zig c++ -target x86_64-linux-musl" \
		STRIP="zig llvm-strip" \
		TARGET="x86_64-linux-musl" \
		OPT="-Oz -flto -ffunction-sections -fdata-sections -static"
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="zig cc -target aarch64-linux-musl" \
		CXX="zig c++ -target aarch64-linux-musl" \
		STRIP="zig llvm-strip" \
		TARGET="aarch64-linux-musl" \
		OPT="-Oz -flto -ffunction-sections -fdata-sections -static"
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="zig cc -target x86_64-windows-gnu" \
		CXX="zig c++ -target x86_64-windows-gnu" \
		STRIP="zig llvm-strip" \
		TARGET="x86_64-windows-gnu.exe" \
		OPT="-Oz -flto -ffunction-sections -fdata-sections -static"
	$(MAKE) NDEBUG=1 -j$(JOBS) \
		CC="zig cc -target aarch64-macos" \
		CXX="zig c++ -target aarch64-macos" \
		STRIP="zig llvm-strip" \
		TARGET="aarch64-macos" \
		OPT="-Oz -flto -ffunction-sections -fdata-sections"

release:
	podman build -t localhost/cxxbuilder buildtools/
	podman run --rm --userns=keep-id \
		-e HOME=/tmp \
		-e ZIG_GLOBAL_CACHE_DIR=/tmp/zig-global-cache \
		-e ZIG_LOCAL_CACHE_DIR=/tmp/zig-local-cache \
		-v ${PWD}:/src:Z localhost/cxxbuilder "/src/buildtools/podman-make-release"

.PHONY: all clean distclean builddir release release-base strip install

-include $(DEPS)
