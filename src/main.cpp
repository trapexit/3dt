/*
  ISC License

  Copyright (c) 2021, Antonio SJ Musumeci <trapexit@spawn.link>

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

#include "subcommand.hpp"

#include "CLI11.hpp"

#include <locale>


static
void
generate_version_argparser(CLI::App &app_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("version","print 3dt version");

  subcmd->callback(std::bind(Subcommand::version));
}

static
void
generate_list_argparser(CLI::App      &app_,
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

  subcmd->callback(std::bind(Subcommand::list,std::cref(options_)));
}

static
void
generate_info_argparser(CLI::App      &app_,
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

  subcmd->callback(std::bind(Subcommand::info,std::cref(options_)));
}

static
void
generate_identify_argparser(CLI::App          &app_,
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

  subcmd->callback(std::bind(Subcommand::identify,std::cref(options_)));
}

static
void
generate_unpack_argparser(CLI::App        &app_,
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
    ->check(CLI::ExistingDirectory)
    ->take_last();

  subcmd->callback(std::bind(Subcommand::unpack,std::cref(options_)));
}

static
void
generate_pack_argparser(CLI::App      &app_,
                        Options::Pack &options_)
{
  CLI::App *subcmd;

  subcmd = app_.add_subcommand("pack","pack a directory into a 3DO disc image");

  subcmd->callback(std::bind(Subcommand::pack,std::cref(options_)));
}

static
void
generate_rename_argparser(CLI::App        &app_,
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

  subcmd->callback(std::bind(Subcommand::rename,std::cref(options_)));
}

static
void
generate_to_iso_argparser(CLI::App       &app_,
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
    ->type_name("PATH")
    ->check(CLI::NonexistentPath);

  subcmd->callback(std::bind(Subcommand::to_iso,std::cref(options_)));
}

static
void
generate_romtags_argparser(CLI::App         &app_,
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

  subcmd->callback(std::bind(Subcommand::romtags,std::cref(opts_)));
}

static
void
generate_argparser(CLI::App &app_,
                   Options  &options_)
{
  app_.set_help_all_flag("--help-all");
  app_.require_subcommand();

  generate_version_argparser(app_);
  generate_list_argparser(app_,options_.list);
  generate_info_argparser(app_,options_.info);
  generate_identify_argparser(app_,options_.identify);
  generate_unpack_argparser(app_,options_.unpack);
  //  generate_pack_argparser(app_,options_.pack);
  generate_rename_argparser(app_,options_.rename);
  generate_to_iso_argparser(app_,options_.to_iso);
  generate_romtags_argparser(app_,options_.romtags);
}

static
void
set_locale()
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
  CLI::App app("3dt: 3DO Disc Tool");

  set_locale();

  generate_argparser(app,options);

  try
    {
      app.parse(argc_,argv_);
    }
  catch(const CLI::ParseError &e)
    {
      return app.exit(e);
    }

  return 0;
}
