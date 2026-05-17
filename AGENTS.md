# AGENTS.md — Coding Agent Guidelines for 3dt

## Project Overview

`3dt` is a pure C/C++17 command-line tool for working with 3DO disc images (OperaFS). All dependencies are vendored in `src/` — no package manager. Release builds are fully static.

- **Language:** C++17 (project logic) + C (legacy crypto/math code)
- **Build system:** GNU Make
- **Key libraries:** CLI11, {fmt}, bigdigits (RSA), md5, crc32b

## Build Commands

```sh
make                  # Debug build: -O0 -ggdb → build/3dt
make clean            # Remove build/ directory
make NDEBUG=1         # Release build: -Os -static
make SANITIZE=1       # Debug with -fsanitize=undefined
make release          # Stripped binaries for Unix + Win32 + Win64

# Cross-compile (require MinGW)
make -f Makefile.win32   # 32-bit Windows
make -f Makefile.win64   # 64-bit Windows
make -f Makefile.macos   # macOS
```

## Lint / Tests

- **No lint/formatter** — only `-Wall` compiler warnings
- **No test framework** — manual testing:
  ```sh
  ./build/3dt --help
  ./build/3dt list <disc.iso>
  ./build/3dt verify <disc.iso>
  ```

## Code Style

### File Layout
- All source flat in `src/` (no path prefix in includes)
- Entry point: `src/main.cpp`
- Each `.cpp` has matching `.hpp`

### Include Order
1. Project headers (quoted): `#include "error.hpp"`
2. Bundled third-party: `#include "fmt.hpp"`
3. Standard library (angle brackets): `#include <vector>`

### Include Guards
- C++: `#pragma once`
- C: `#ifndef`/`#define`/`#endif` guards

### Naming Conventions

| Entity | Convention | Example |
|--------|------------|---------|
| Classes/Namespaces/Structs | `PascalCase` | `DiscUnpacker`, `TDO` |
| Members | `snake_case` | `volume_block_count` |
| Private members | `_snake_case` | `_impl`, `_stream` |
| **Function parameters** | `snake_case_` (trailing `_`) | `filepath_`, `opts_` |
| Functions | `snake_case` | `disc_label()`, `open()` |
| Locals | `snake_case` | `block_count` |
| Macros | `ALL_CAPS` | `DR_FLAG_IS_DIRECTORY` |
| Type aliases | Short or PascalCase | `u8`, `s32`, `PathVec` |

### Brace Style (Allman/BSD, 2-space indent)
```cpp
void
Foo::bar(const std::filesystem::path &path_)
{
  if(condition)
    {
      do_something();
    }
  else
    {
      do_other();
    }
}
```
- Return type on separate line
- Always brace `if`/`else`/`for`/`while` bodies
- No space: `if(cond)`, `func()`

### Namespaces
- `namespace fs = std::filesystem;` in `.cpp` only
- Project namespaces: `TDO`, `Subcommand`, `Log`
- Anonymous namespace for file-local functions

### Error Handling
Use custom `Error` struct (not exceptions for app errors):
```cpp
Error err;
err = stream.open(filepath_);
if(err)
  return Log::error(err);
return {};  // success
```

### Output
- User output: `fmt::print(...)` via `#include "fmt.hpp"`
- Never use `std::cout`
- Errors: `fprintf(stderr, ...)` via `Log::error()`

### Types
- Use `std::uint32_t` or shorthand: `u8`, `s32`, `u64`, `f64` (from `types_ints.h`)
- `std::filesystem::path` for paths
- `std::unique_ptr` for ownership
- `std::optional` for optional values
- Keep C arrays in on-disc structs

### C vs C++ Files
- C files: use `EXTERN_C_BEGIN`/`EXTERN_C_END` from `extern_c.h`; traditional include guards
- C++ files: `#pragma once`; C++17 features freely

### Subcommand Pattern
Each subcommand in `subcommand_<name>.cpp`, declared in `subcommand.hpp` under `namespace Subcommand`.

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry point, CLI11 setup |
| `src/options.hpp` | Nested option structs |
| `src/subcommand.hpp` | Subcommand declarations |
| `src/error.hpp` | `Error` struct |
| `src/tdo_dev_stream.hpp` | Disc image stream abstraction |
| `src/types_ints.h` | Integer typedefs |
