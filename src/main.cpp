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
#include "log.hpp"
#include "subcmd.hpp"
#include "tdo_rsa.h"
#include "version.hpp"

#include "CLI11.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


static
void
_generate_version_argparser(CLI::App &app_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("version","print 3dt version");

  subcmd->callback([]()
  {
    Subcmd::version();
  });
}

static
void
_generate_list_argparser(CLI::App      &app_,
                         Options::List &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("list","list disc content");

  subcmd->add_option("filepath",options_.filepath)
    ->description("path to disc image")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("prefix-filter",options_.prefix_filter)
    ->description("filter out paths with shared starting path");
  subcmd->add_option("-f,--format",options_.format)
    ->description("output format")
    ->type_name("TEXT")
    ->default_val("default")
    ->take_last()
    ->check(CLI::IsMember({"default","file-offsets","block-offsets"}));

  subcmd->callback([&options_]()
  {
    Subcmd::list(options_);
  });
}

static
void
_generate_info_argparser(CLI::App      &app_,
                         Options::Info &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("info","prints lowlevel info on disc");
  subcmd->add_option("filepaths",options_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("-f,--format",options_.format)
    ->description("output format")
    ->type_name("TEXT")
    ->default_val("human")
    ->take_last()
    ->check(CLI::IsMember({"human","csv","cheader"}));

  subcmd->callback([&options_]()
  {
    Subcmd::info(options_);
  });
}

static
void
_generate_identify_argparser(CLI::App          &app_,
                             Options::Identify &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("identify","attempt to identify disc image");
  subcmd->add_option("filepaths",options_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("-f,--format",options_.format)
    ->description("output format")
    ->type_name("TEXT")
    ->default_val("human")
    ->take_last()
    ->check(CLI::IsMember({"human","csv"}));

  subcmd->callback([&options_]()
  {
    Subcmd::identify(options_);
  });
}

static
void
_generate_unpack_argparser(CLI::App        &app_,
                           Options::Unpack &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("unpack","unpack disc image");
  subcmd->add_option("<filepaths>",options_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("-f,--format",options_.format)
    ->description("logging format")
    ->type_name("TEXT")
    ->default_val("human")
    ->take_last()
    ->check(CLI::IsMember({"human","csv"}));
  subcmd->add_option("-o,--output",options_.output)
    ->description("output directory")
    ->type_name("PATH")
    ->default_val("")
    ->take_last();
  subcmd->add_option("--layout",options_.layout)
    ->description("layout metadata output file (default: layout.json in unpacked root)")
    ->type_name("PATH")
    ->default_val("")
    ->take_last();

  subcmd->callback([&options_]()
  {
    Subcmd::unpack(options_);
  });
}

static
void
_generate_pack_argparser(CLI::App      &app_,
                         Options::Pack &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("pack","pack a directory into a 3DO disc image");
  subcmd->add_option("filepath",options_.input)
    ->description("path to source directory")
    ->type_name("PATH")
    ->check(CLI::ExistingDirectory)
    ->required();
  subcmd->add_option("-o,--output",options_.output)
    ->description("output ISO file")
    ->type_name("PATH")
    ->required()
    ->take_last();
  subcmd->add_option("--layout",options_.layout)
    ->description("layout metadata input file (default: layout.json in source root when present)")
    ->type_name("PATH")
    ->default_val("")
    ->take_last();
  subcmd->add_option("--volume-label",options_.volume_label)
    ->description("disc volume label")
    ->type_name("TEXT")
    ->default_val("CD-ROM")
    ->take_last();
  subcmd->add_option("--volume-commentary",options_.volume_commentary)
    ->description("disc volume commentary")
    ->type_name("TEXT")
    ->default_val("")
    ->take_last();
  subcmd->add_option("--volume-unique-id",options_.volume_unique_identifier)
    ->description("disc volume unique identifier (0 selects random),"
                  " defaults to crc32b of BannerScreen")
    ->type_name("UINT")
    ->each([&options_](std::string){ options_.volume_unique_identifier_set = true; })
    ->take_last();
  subcmd->add_option("--root-unique-id",options_.root_unique_identifier)
    ->description("root directory unique identifier (0 selects random),"
                  " defaults to crc32b of LaunchMe")
    ->type_name("UINT")
    ->each([&options_](std::string){ options_.root_unique_identifier_set = true; })
    ->take_last();
  subcmd->add_flag("--dry-run",options_.dry_run)
    ->description("validate and report without writing an image");
  subcmd->add_flag("--no-banner-romtag{false},--no-rsa-appsplash{false}",
                   options_.banner_romtag)
    ->description("do not generate an RSA_APPSPLASH ROMTag for BannerScreen");
  subcmd->add_flag("--billstuff-romtag",
                   options_.billstuff_romtag)
    ->description("generate an RSA_BILLSTUFF ROMTag");
  subcmd->add_option("--digest-check-count",
                     options_.signature_digest_check_count)
    ->description("RSA_SIGNATURE_BLOCK TypeSpecific digest check count")
    ->type_name("UINT")
    ->check(CLI::Range(0,255))
    ->default_val("0")
    ->take_last();
  subcmd->add_option("--mark",options_.mark)
    ->description("write a 3dt marker into the output image")
    ->type_name("BOOL")
    ->default_val("true")
    ->take_last();
  subcmd->add_flag("--sign",options_.sign)
    ->description("sign the image after packing");

  subcmd->callback([&options_]()
  {
    Subcmd::pack(options_);
  });
}

static
void
_generate_repack_argparser(CLI::App        &app_,
                           Options::Repack &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("repack","repack a 3DO disc image compacting avatars and empty space");
  subcmd->add_option("filepaths",options_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("-o,--output",options_.output)
    ->description("output image path; requires exactly one input")
    ->type_name("PATH")
    ->take_last();
  subcmd->add_flag("--no-banner-romtag{false},--no-rsa-appsplash{false}",
                   options_.banner_romtag)
    ->description("do not generate an RSA_APPSPLASH ROMTag for BannerScreen");
  subcmd->add_flag("--billstuff-romtag",
                   options_.billstuff_romtag)
    ->description("generate an RSA_BILLSTUFF ROMTag");
  subcmd->add_option("--digest-check-count",
                     options_.signature_digest_check_count)
    ->description("RSA_SIGNATURE_BLOCK TypeSpecific digest check count")
    ->type_name("UINT")
    ->check(CLI::Range(0,255))
    ->default_val("0")
    ->take_last();
  subcmd->add_option("--mark",options_.mark)
    ->description("write a 3dt marker into the output image")
    ->type_name("BOOL")
    ->default_val("true")
    ->take_last();
  subcmd->add_flag("--sign",options_.sign)
    ->description("sign the image after repacking");

  subcmd->callback([&options_]()
  {
    Subcmd::repack(options_);
  });
}

static
void
_generate_rename_argparser(CLI::App        &app_,
                           Options::Rename &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("rename","rename disc image as identified");
  subcmd->add_option("filepaths",options_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_flag("-t,--take-first",options_.take_first)
    ->description("if there are multiple matches use the first one found");

  subcmd->callback([&options_]()
  {
    Subcmd::rename(options_);
  });
}

static
void
_generate_to_iso_argparser(CLI::App       &app_,
                           Options::ToISO &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("to-iso","convert a .bin or similar disc image to .iso");
  subcmd->add_option("filepath",options_.input)
    ->description("path to disc image")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("output",options_.output)
    ->description("output file")
    ->type_name("PATH");
  subcmd->add_flag("--force",options_.force)
    ->description("overwrite output file if it already exists");

  subcmd->callback([&options_]()
  {
    Subcmd::to_iso(options_);
  });
}

static
void
_generate_romtags_argparser(CLI::App         &app_,
                            Options::ROMTags &opts_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("romtags","print out image romtags");
  subcmd->add_option("filepaths",opts_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("-f,--format",opts_.format)
    ->description("output format")
    ->type_name("TEXT")
    ->default_val("human")
    ->take_last()
    ->check(CLI::IsMember({"human","csv"}));

  subcmd->callback([&opts_]()
  {
    Subcmd::romtags(opts_);
  });
}

static
void
_generate_verify_argparser(CLI::App        &app_,
                           Options::Verify &opts_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("verify","verify RSA sigs");
  subcmd->add_option("filepaths",opts_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("-f,--format",opts_.format)
    ->description("output format")
    ->type_name("TEXT")
    ->default_val("human")
    ->take_last()
    ->check(CLI::IsMember({"human","csv","json"}));
  subcmd->add_flag("--no-digest-table{false}",opts_.digest_table)
    ->description("skip signature digest table comparison");
  subcmd->add_flag("--quiet",opts_.quiet)
    ->description("print only per-image verification status");

  subcmd->callback([&opts_]()
  {
    Subcmd::verify(opts_);
  });
}

static
void
_generate_sign_argparser(CLI::App      &app_,
                         Options::Sign &opts_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("sign","sign 3DO ISO for retail system use");
  subcmd->add_option("filepaths",opts_.filepaths)
    ->description("path to disc images")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_option("-o,--output",opts_.output)
    ->description("output image path; requires exactly one input")
    ->type_name("PATH")
    ->take_last();
  subcmd->add_flag("--no-banner-romtag{false},--no-rsa-appsplash{false}",
                   opts_.banner_romtag)
    ->description("do not generate an RSA_APPSPLASH ROMTag for BannerScreen");
  subcmd->add_flag("--billstuff-romtag",
                   opts_.billstuff_romtag)
    ->description("generate an RSA_BILLSTUFF ROMTag");
  subcmd->add_flag("--force",opts_.force)
    ->description("skip signing preflight checks for unusual images");
  subcmd->add_option("--digest-check-count",
                     opts_.signature_digest_check_count)
    ->description("RSA_SIGNATURE_BLOCK TypeSpecific digest check count")
    ->type_name("UINT")
    ->check(CLI::Range(0,255))
    ->default_val("0")
    ->take_last();
  subcmd->add_option("--mark",opts_.mark)
    ->description("write a 3dt marker into the signed image")
    ->type_name("BOOL")
    ->default_val("true")
    ->take_last();

  subcmd->callback([&opts_]()
  {
    Subcmd::sign(opts_);
  });
}

static
void
_generate_signfile_argparser(CLI::App          &app_,
                             Options::SignFile &opts_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("sign-file","sign file with 3DO or APP key");
  subcmd->add_option("filepaths",opts_.filepaths)
    ->description("path to file to sign")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();
  subcmd->add_flag("--append",opts_.append)
    ->description("append a new signature instead of replacing the existing trailer");
  subcmd->add_flag("--replace",opts_.replace)
    ->description("replace the existing signature trailer");
  subcmd->add_flag("--verify",opts_.verify)
    ->description("verify the existing signature trailer");
  subcmd->add_flag("--write",opts_.write)
    ->description("write the computed signature to the file");
  subcmd->add_option("--signature-output",opts_.signature_output)
    ->description("write computed signature bytes to a separate file; requires one input")
    ->type_name("PATH")
    ->take_last();
  subcmd->add_option("--key-name",opts_.key_name)
    ->default_val(TDO_KEY_APP)
    ->check(CLI::IsMember({TDO_KEY_APP,TDO_KEY_3DO}));

  subcmd->callback([&opts_]()
  {
    Subcmd::sign_file(opts_);
  });
}

static
void
_generate_decryptfile_argparser(CLI::App         &app_,
                                Options::DecFile &opts_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("decrypt-file","decrypt CD-DIPIR boot payload (`src/dipir/cdipir.c`)");
  subcmd->add_option("filepaths",opts_.filepaths)
    ->description("path to file to decrypt")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();

  subcmd->callback([&opts_]()
  {
    Subcmd::decrypt_file(opts_);
  });
}

static
void
_generate_encryptfile_argparser(CLI::App         &app_,
                                Options::EncFile &opts_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("encrypt-file","encrypt CD-DIPIR boot payload (`src/dipir/cdipir.c`)");
  subcmd->add_option("filepaths",opts_.filepaths)
    ->description("path to file to encrypt")
    ->type_name("PATH")
    ->check(CLI::ExistingFile)
    ->required();

  subcmd->callback([&opts_]()
  {
    Subcmd::encrypt_file(opts_);
  });
}

static
void
_generate_argparser(CLI::App &app_,
                    Options  &options_)
{
  app_.set_help_all_flag("--help-all");
  app_.require_subcommand();

  _generate_version_argparser(app_);
  _generate_list_argparser(app_,options_.list);
  _generate_info_argparser(app_,options_.info);
  _generate_identify_argparser(app_,options_.identify);
  _generate_unpack_argparser(app_,options_.unpack);
  _generate_pack_argparser(app_,options_.pack);
  _generate_repack_argparser(app_,options_.repack);
  _generate_rename_argparser(app_,options_.rename);
  _generate_to_iso_argparser(app_,options_.to_iso);
  _generate_romtags_argparser(app_,options_.romtags);
  _generate_verify_argparser(app_,options_.verify);
  _generate_sign_argparser(app_,options_.sign);
  _generate_signfile_argparser(app_,options_.signfile);
  _generate_decryptfile_argparser(app_,options_.decryptfile);
  _generate_encryptfile_argparser(app_,options_.encryptfile);
}

static
void
_set_locale()
{
  try
    {
      std::locale::global(std::locale(""));
    }
  catch(const std::runtime_error &e)
    {
      std::locale::global(std::locale("C"));
    }
}

int
main(int    argc_,
     char **argv_)
{
  Options options;
  CLI::App app("3dt: 3DO Disc Tool (v" VERSION ")");

  app.name("Usage: 3dt");

  _set_locale();

  _generate_argparser(app,options);

  if(argc_ == 1)
    {
      std::cout << app.help() << std::endl;
      return 0;
    }

  try
    {
      app.parse(argc_,argv_);
    }
  catch(const CLI::ParseError &e)
    {
      return app.exit(e);
    }
  catch(const std::exception &e)
    {
      Log::error({e.what()});
      return 1;
    }

  return 0;
}
