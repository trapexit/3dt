COMPILER_PREFIX =
PLATFORM = unix
EXE = 3dt

JOBS := $(shell nproc)

OUTPUT = build/$(EXE)

.DEFAULT_GOAL := all

CC    = $(COMPILER_PREFIX)-gcc
CXX   = $(COMPILER_PREFIX)-g++
STRIP = $(COMPILER_PREFIX)-strip

ifeq ($(NDEBUG),1)
BUILD_MODE := release
OPT := -Os -static
else
BUILD_MODE := debug
OPT := -O0 -ggdb
endif

ifeq ($(SANITIZE),1)
BUILD_MODE := $(BUILD_MODE)-sanitize
OPT += -fsanitize=undefined
endif

CFLAGS = $(OPT) -Wall
CXXFLAGS = $(OPT) -Wall -std=c++17
CPPFLAGS ?= -MMD -MP

SRCS_C   := $(wildcard src/*.c)
SRCS_CXX := $(wildcard src/*.cpp)

BUILDDIR = build/$(PLATFORM)/$(BUILD_MODE)
OBJS := $(SRCS_C:src/%.c=$(BUILDDIR)/%.c.o)
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
	@echo "  release   Build stripped binaries for Unix, Win32, Win64"
	@echo "  help      Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  NDEBUG=1      Release build (-Os -static)"
	@echo "  SANITIZE=1    Add -fsanitize=undefined"
	@echo ""
	@echo "Cross-compile:"
	@echo "  make -f Makefile.win32    32-bit Windows (MinGW)"
	@echo "  make -f Makefile.win64    64-bit Windows (MinGW)"
	@echo "  make -f Makefile.macos    macOS"
	@echo ""
	@echo "Output: $(OUTPUT)"

all: $(OUTPUT)

$(OUTPUT): builddir $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(OUTPUT) $(OBJS) $(LDFLAGS)

strip: $(OUTPUT)
	$(STRIP) --strip-all $(OUTPUT)

$(BUILDDIR)/%.c.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.cpp.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rfv build/

distclean:
	git clean -xfd

builddir:
	mkdir -p $(BUILDDIR)

release:
	$(MAKE) -f Makefile -j$(JOBS) NDEBUG=1 strip
	$(MAKE) -f Makefile.win32 -j$(JOBS) strip
	$(MAKE) -f Makefile.win64 -j$(JOBS) strip

.PHONY: clean distclean builddir release

-include $(DEPS)
