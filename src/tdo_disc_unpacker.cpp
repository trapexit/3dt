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

#include <array>
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
      _dstpath(),
      _include_metadata(true),
      _include_system(true),
      _include_executables(true)
  {
  }

  ~Impl()
  {
  }

public:
  void
  unpack(const fs::path &dstpath_,
         const bool      include_system_,
         const bool      include_metadata_,
         const bool      include_executables_)
  {
    _dstpath             = dstpath_;
    _include_metadata    = include_metadata_;
    _include_system      = include_system_;
    _include_executables = include_executables_;
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
    if(_should_skip_system(path_))
      return;

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
    if(_should_skip_system(path_) ||
       _should_skip_metadata(path_,record_) ||
       _should_skip_executable(record_,stream_))
      return;

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
    if(_should_skip_system(parent_))
      return Error();

    const fs::path path = TDO::display_path(parent_,filename_);

    if(_should_skip_metadata(path,record_) ||
       _should_skip_executable(record_,stream_))
      return Error();

    _cb.before(path,record_,dr_file_pos_,stream_);
    _cb.after(path,record_,1);
    fmt::print(stderr,"3dt: {} - {}\n",err_.str,path.generic_string());

    return Error();
  }

private:
  static
  bool
  _name_matches(const fs::path &component_,
                const char     *expected_,
                const std::size_t expected_size_)
  {
    const auto &name = component_.native();

    if(name.size() != expected_size_)
      return false;

    for(std::size_t i = 0; i < name.size(); ++i)
      {
        fs::path::value_type c = name[i];

        if((c >= 'A') && (c <= 'Z'))
          c += 'a' - 'A';
        if(c != expected_[i])
          return false;
      }

    return true;
  }

  static
  std::uint32_t
  _read_u32_be(const std::array<char,0x80> &data_,
               const std::size_t            offset_)
  {
    return ((static_cast<std::uint32_t>(
               static_cast<unsigned char>(data_[offset_ + 0])) << 24) |
            (static_cast<std::uint32_t>(
               static_cast<unsigned char>(data_[offset_ + 1])) << 16) |
            (static_cast<std::uint32_t>(
               static_cast<unsigned char>(data_[offset_ + 2])) << 8) |
            (static_cast<std::uint32_t>(
               static_cast<unsigned char>(data_[offset_ + 3])) << 0));
  }

  static
  bool
  _is_valid_bl(const std::uint32_t instruction_,
               const std::uint64_t instruction_offset_)
  {
    std::int64_t immediate;
    std::int64_t target;

    if((instruction_ & 0x0f000000) != 0x0b000000)
      return false;

    immediate = instruction_ & 0x00ffffff;
    if((immediate & 0x00800000) != 0)
      immediate -= 0x01000000;

    target = static_cast<std::int64_t>(instruction_offset_) + 8 +
             (immediate * 4);
    return (target >= 0);
  }

  static
  bool
  _is_aif_executable(const TDO::DirectoryRecord &record_,
                     TDO::DevStream              &stream_)
  {
    static constexpr std::uint32_t ARM_NOP = 0xe1a00000;
    static constexpr std::uint32_t AIF_EXIT_INSTRUCTION = 0xef000011;
    static constexpr std::uint32_t AIF_3DO_HEADER_FLAG = 0x40000000;
    static constexpr std::uint64_t AIF_HEADER_SIZE = 0x80;
    static constexpr std::uint64_t AIF_3DO_HEADER_SIZE = 0xb8;

    std::array<char,AIF_HEADER_SIZE> header;
    std::uint64_t byte_pos;
    std::uint64_t header_size;
    std::uint64_t image_size;
    std::uint32_t address_mode;
    std::uint32_t debug_size;
    std::uint32_t decompress_instruction;
    std::uint32_t entry_instruction;
    std::uint32_t ro_size;
    std::uint32_t rw_size;
    std::uint32_t self_reloc_instruction;
    std::uint32_t workspace;
    std::uint32_t zero_init_instruction;

    if(record_.is_directory() ||
       (record_.byte_count < header.size()) ||
       record_.avatar_list.empty())
      return false;

    byte_pos = static_cast<std::uint64_t>(record_.avatar_list[0]) *
               stream_.device_block_data_size();
    stream_.read_data_bytes(header.data(),
                            static_cast<s64>(byte_pos),
                            static_cast<s64>(header.size()));

    decompress_instruction = _read_u32_be(header,0x00);
    self_reloc_instruction = _read_u32_be(header,0x04);
    zero_init_instruction = _read_u32_be(header,0x08);
    entry_instruction = _read_u32_be(header,0x0c);

    if(_read_u32_be(header,0x10) != AIF_EXIT_INSTRUCTION)
      return false;
    if((decompress_instruction != ARM_NOP) &&
       !_is_valid_bl(decompress_instruction,0x00))
      return false;
    if((self_reloc_instruction != ARM_NOP) &&
       !_is_valid_bl(self_reloc_instruction,0x04))
      return false;
    if((zero_init_instruction != ARM_NOP) &&
       !_is_valid_bl(zero_init_instruction,0x08))
      return false;
    if(((entry_instruction >> 24) != 0xeb) ||
       !_is_valid_bl(entry_instruction,0x0c))
      return false;

    address_mode = _read_u32_be(header,0x30);
    if((address_mode != 26) && (address_mode != 32))
      return false;

    ro_size = _read_u32_be(header,0x14);
    rw_size = _read_u32_be(header,0x18);
    debug_size = _read_u32_be(header,0x1c);
    workspace = _read_u32_be(header,0x2c);
    header_size = ((workspace & AIF_3DO_HEADER_FLAG) != 0)
      ? AIF_3DO_HEADER_SIZE
      : AIF_HEADER_SIZE;
    if(ro_size < header_size)
      return false;

    image_size = (static_cast<std::uint64_t>(ro_size) +
                  static_cast<std::uint64_t>(rw_size) +
                  static_cast<std::uint64_t>(debug_size));

    return (image_size <= record_.byte_count);
  }

  bool
  _should_skip_executable(const TDO::DirectoryRecord &record_,
                          TDO::DevStream              &stream_) const
  {
    if(_include_executables)
      return false;

    return ((record_.type == DR_TYPE_CATAPULT) ||
            _is_aif_executable(record_,stream_));
  }

  bool
  _should_skip_metadata(const fs::path             &path_,
                        const TDO::DirectoryRecord &record_) const
  {
    fs::path::iterator first;
    fs::path::iterator next;

    if(_include_metadata || record_.is_directory())
      return false;

    first = path_.begin();
    if(first == path_.end())
      return false;

    next = first;
    ++next;
    if(next != path_.end())
      return false;

    return (_name_matches(*first,"disc label",sizeof("disc label") - 1) ||
            _name_matches(*first,"layout.json",sizeof("layout.json") - 1) ||
            _name_matches(*first,"rom_tags",sizeof("rom_tags") - 1) ||
            _name_matches(*first,"signatures",sizeof("signatures") - 1));
  }

  bool
  _should_skip_system(const fs::path &path_) const
  {
    fs::path::iterator first;

    if(_include_system)
      return false;

    first = path_.begin();
    return ((first != path_.end()) &&
            _name_matches(*first,"system",sizeof("system") - 1));
  }

private:
  TDO::DiscUnpacker::Callback &_cb;
  TDO::FSWalker                _walker;

private:
  fs::path _dstpath;
  bool     _include_metadata;
  bool     _include_system;
  bool     _include_executables;
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
  DiscUnpacker::unpack(const fs::path &dstpath_,
                       const bool      include_system_,
                       const bool      include_metadata_,
                       const bool      include_executables_)
  {
    fs::create_directories(dstpath_);

    _impl->unpack(dstpath_,
                  include_system_,
                  include_metadata_,
                  include_executables_);
  }
}
