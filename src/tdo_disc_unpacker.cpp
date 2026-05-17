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

#include "error.hpp"
#include "tdo_directory_header.hpp"
#include "tdo_directory_record.hpp"
#include "tdo_disc_format.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_disc_unpacker.hpp"
#include "tdo_safe_narrow.hpp"

#include "fmt.hpp"

#include <fstream>
#include <cctype>
#include <vector>


namespace fs = std::filesystem;

class TDO::DiscUnpacker::Impl final : public TDO::FSWalker::Callbacks
{
public:
  Impl(std::iostream               &ios_,
       TDO::DiscUnpacker::Callback &cb_)
    : _cb(cb_),
      _walker(ios_,*this),
      _dstpath()
  {
  }

  ~Impl()
  {
  }

public:
  void
  unpack(const fs::path &dstpath_)
  {
    _dstpath = dstpath_;
    _walker.walk();
  }

public:
  void
  begin()
  {
    _cb.begin();
  }

  void
  end()
  {
    _cb.end();
  }


public:
  void
  operator()(const std::filesystem::path &path_,
             const TDO::DirectoryHeader  &header_,
             TDO::DevStream              &stream_)
  {
    // file_tell() returns s64; sizeof(TDO::DirectoryHeader) is size_t.
    // The DiscUnpacker::Callback::directory third parameter is u32.
    // With images that can exceed 4 GiB (the v2 walker permits
    // intermediate positions up to s64), the implicit narrowing
    // through unsigned arithmetic would silently wrap. Compute the
    // header position in s64 and route through checked_narrow.
    const s64 header_pos =
      stream_.file_tell() - static_cast<s64>(sizeof(TDO::DirectoryHeader));
    _cb.directory(path_,
                  header_,
                  TDO::checked_narrow_s64_to_u32(header_pos,
                                                 "directory header position"),
                  stream_);
  }

  void
  operator()(const std::filesystem::path &path_,
             const TDO::DirectoryRecord  &record_,
             const std::uint32_t          dr_file_pos_,
             TDO::DevStream              &stream_)
  {
    fs::path fullpath = _dstpath / path_;

    {
      std::error_code ec_dst;
      std::error_code ec_full;
      const fs::path canonical_dst  = fs::weakly_canonical(_dstpath,ec_dst);
      const fs::path canonical_full = fs::weakly_canonical(fullpath,ec_full);
      // Fail closed: a single shared error_code would let the second
      // call's success silently clear a failure on the first, and the
      // empty canonical_dst would then make every prefix comparison
      // pass. Reject any case where we cannot determine containment.
      if(ec_dst || ec_full || canonical_dst.empty())
        throw Error("refusing to write: cannot canonicalize destination path: " +
                    fullpath.string());

      const std::string a = canonical_dst.generic_string();
      const std::string b = canonical_full.generic_string();
      const bool same = (a == b);
      const bool prefixed = ((b.size() > a.size()) &&
                             (b.compare(0,a.size(),a) == 0) &&
                             (a.back() == '/' || b[a.size()] == '/'));
      if(!same && !prefixed)
        throw Error("refusing to write outside destination: " +
                    fullpath.string());
    }

    _cb.before(path_,record_,dr_file_pos_,stream_);
    if(record_.is_directory())
      {
        fs::create_directories(fullpath);
      }
    else
      {
        std::ofstream os;
        std::uint64_t bytes_left;
        std::uint64_t byte_pos;
        std::vector<char> buf;

        bytes_left = record_.byte_count;
        if((bytes_left > 0) && record_.avatar_list.empty())
          throw Error("file record has byte_count > 0 but no avatars: " +
                      fullpath.string());

        os.open(fullpath,std::ios::binary|std::ios::trunc);
        if(!os.is_open())
          throw Error("failed to open output file: " + fullpath.string());

        if(bytes_left > 0)
          {
            byte_pos = static_cast<std::uint64_t>(record_.avatar_list[0]) *
                       stream_.device_block_data_size();
            buf.resize(std::min<std::uint64_t>(bytes_left,64*1024));
          }
        else
          {
            byte_pos = 0;
          }

        while(bytes_left > 0)
          {
            const std::uint64_t n = std::min<std::uint64_t>(bytes_left,buf.size());
            stream_.read_data_bytes(buf.data(),
                                    static_cast<s64>(byte_pos),
                                    static_cast<s64>(n));
            os.write(buf.data(),n);
            if(!os)
              throw Error("failed to write output file: " + fullpath.string());
            byte_pos   += n;
            bytes_left -= n;
          }

        os.close();
        if(os.fail())
          throw Error("failed to close output file: " + fullpath.string());
      }
    _cb.after(path_,record_,0);
  }

  Error
  invalid_filename(const std::filesystem::path &parent_,
                   const std::string           &filename_,
                   const TDO::DirectoryRecord  &record_,
                   const std::uint32_t          dr_file_pos_,
                   const Error                 &err_,
                   TDO::DevStream              &stream_)
  {
    const fs::path path = TDO::display_path(parent_,filename_);

    _cb.before(path,record_,dr_file_pos_,stream_);
    _cb.after(path,record_,1);
    fmt::print(stderr,"3dt: {} - {}\n",err_.str,path.generic_string());

    return Error();
  }

private:
  TDO::DiscUnpacker::Callback &_cb;
  TDO::FSWalker                _walker;

private:
  fs::path _dstpath;
};

namespace TDO
{
  DiscUnpacker::DiscUnpacker(std::iostream &ios_,
                             Callback      &cb_)
  {
    _impl = std::make_unique<Impl>(ios_,cb_);
  }

  DiscUnpacker::~DiscUnpacker()
  {
  }

  void
  DiscUnpacker::unpack(const fs::path &dstpath_)
  {
    fs::create_directories(dstpath_);

    _impl->unpack(dstpath_);
  }
}
