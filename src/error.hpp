/*
  ISC License

  Copyright (c) 2025, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#pragma once

#include <string>

struct Error
{
  Error()
    : code(1)
  {
  }

  Error(const Error &err_)
    : str(err_.str),
      code(err_.code)
  {
  }

  Error(Error &&err_)
    : str(std::move(err_.str)),
      code(err_.code)
  {
  }

  Error(const char *str_)
    : str(str_),
      code(1)
  {
  }

  Error(const std::string &str_)
    : str(str_),
      code(1)
  {
  }

  Error(const char *str_,
        const int   code_)
    : str(str_),
      code(code_)
  {
  }

  Error(const std::string &str_,
        const int          code_)
    : str(str_),
      code(code_)
  {
  }

  Error& operator=(const Error &err_) { str = err_.str; code = err_.code; return *this; }
  Error& operator=(const std::string &str_) { str = str_; code = 1; return *this; }

  operator bool() { return !str.empty(); }
  operator const std::string&() const { return str; }
  operator const char*() const { return str.c_str(); }

  const char *c_str() const { return str.c_str(); }


  std::string str;
  int         code;
};
