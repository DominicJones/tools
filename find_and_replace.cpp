// g++17 -O3 find_and_replace.cpp -o find_and_replace.exe
//
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <iterator>
#include <regex>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <cctype>


/*
  "(DrvMode|DrvComponent|DrvOperation)::Option" -> "$1"
  "\\bPrimal\\b" -> "DrvPrimal"
  "\\b(Tangent|Adjoint)Drv\\b" -> "Drv$1"
  "(DrvStorageHelper.*)remove(Storage|AllStorage)" -> "$1clear$2"
  "DrvSequence\\.h" -> "DrvHeaders.h"
  "( *).*DrvDualSweep\\{" -> "$1DrvDualSweep("
  "(DrvDualSweepOption::.*_op\\)?)\\};" -> "$1);"
  "Field<DrvPrimal<VectorProfileVar<(3|ND)> *>" -> "Field<VectorProfileVar<$1>"
  "([^\\.a-zA-Z0-9_:])(primal|primal_if|tangent|adjoint)\\(" -> "$1drv::$2("
*/
bool find_and_replace(std::string const &in, std::string const &out, std::string &text)
{
  bool is_modified = false;

  std::regex const rx(in);
  std::smatch match;

  if (std::regex_search(text, match, rx))
  {
    is_modified = true;
    text = std::regex_replace(text, rx, out);
  }

  return is_modified;
}


bool has_option(std::vector<std::string> const &args,
                std::string const &option_name, int &iarg)
{
  std::string rx(option_name);
  std::regex option_regex("^" + rx + "$");
  for (auto it = args.begin(); it != args.end(); ++it)
  {
    if (std::regex_search(std::string(*it), option_regex))
    {
      iarg = std::max(iarg, int(std::distance(args.begin(), it)));
      return true;
    }
  }

  return false;
}


std::optional<std::string>
get_option(std::vector<std::string> const &args,
           std::string const &option_name, int &iarg,
           std::string const &default_value = "")
{
  std::string rx(option_name);
  std::regex option_regex("^" + rx + "$");
  for (auto it = args.begin(), end = args.end(); it != end; ++it)
  {
    if (std::regex_search(std::string(*it), option_regex))
    {
      iarg = std::max(iarg, int(std::distance(args.begin(), it)));
      if (it + 1 != end)
      {
        ++iarg;
        return *(it + 1);
      }
    }
  }

  if (!default_value.empty()) return {default_value};

  return std::nullopt;
}


std::optional<std::string>
get_repeat_option(std::string const &arg,
                  std::string const &option_name)
{
  std::string option = arg;
  std::smatch option_match;
  std::regex option_regex("^" + option_name + "=(\\w+)");
  if (std::regex_search(option, option_match, option_regex))
    return option_match[1];

  return std::nullopt;
}


int main(int argc, char** argv)
{
  auto const args = std::vector<std::string>(argv, argv + argc);

  // options
  auto show_help_opt = std::make_pair("-h|--help", "print help");
  auto ext_opt = std::make_pair("--ext", "file extension regex");
  auto input_opt = std::make_pair("--in", "input regex");
  auto output_opt = std::make_pair("--out", "output regex");
  auto mv_file_opt = std::make_pair("-mf", "rename from --in to --out");
  auto is_verbose_opt = std::make_pair("-v|--verbose", "print status");
  auto fix_eof_newline_opt = std::make_pair("-n|--fix-eof-newline", "fix missing end of file newline character");
  auto exclude_dir_opt = std::make_pair("--exclude-dir", "exclude directory from file...");

  auto iarg = int{0};
  auto const show_help = has_option(args, show_help_opt.first, iarg);
  auto const is_verbose = has_option(args, is_verbose_opt.first, iarg);
  auto const fix_eof_newline = has_option(args, fix_eof_newline_opt.first, iarg);
  auto const ext_str = get_option(args, ext_opt.first, iarg, "\\.(h|cpp)$");
  auto const input_str = get_option(args, input_opt.first, iarg);
  auto const output_str = get_option(args, output_opt.first, iarg);
  auto const mv_file = has_option(args, mv_file_opt.first, iarg);

  std::vector<std::string> exclude_dirs;
  for (int it = 0; it != argc; ++it)
  {
    auto const exclude_dir = get_repeat_option(args[it], exclude_dir_opt.first);
    if (exclude_dir.has_value())
    {
      iarg = std::max(iarg, it);
      exclude_dirs.push_back(exclude_dir.value());
    }
  }
  auto const exclude_any_dir = !exclude_dirs.empty();

  if (show_help || !input_str.has_value() || !output_str.has_value())
  {
    std::cout << "Usage: find_and_replace [options] file..." << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  " << std::setw(28) << std::left << show_help_opt.first << show_help_opt.second << std::endl;
    std::cout << "  " << std::setw(28) << std::left << ext_opt.first << ext_opt.second << std::endl;
    std::cout << "  " << std::setw(28) << std::left << input_opt.first << input_opt.second << std::endl;
    std::cout << "  " << std::setw(28) << std::left << output_opt.first << output_opt.second << std::endl;
    std::cout << "  " << std::setw(28) << std::left << mv_file_opt.first << mv_file_opt.second << std::endl;
    std::cout << "  " << std::setw(28) << std::left << is_verbose_opt.first << is_verbose_opt.second << std::endl;
    std::cout << "  " << std::setw(28) << std::left << fix_eof_newline_opt.first << fix_eof_newline_opt.second << std::endl;
    std::cout << "  " << std::setw(28) << std::left << (exclude_dir_opt.first + std::string("=<arg>")) << exclude_dir_opt.second << std::endl;
    return 0;
  }


  std::vector<std::string> filenames;
  filenames.reserve(100000);


  std::unordered_set<std::string> modfilenames;



  // make file list
  {
    std::regex const extension_regex(ext_str.value());

    for (auto it = iarg + 1; it < argc; ++it)
    {
      auto const fs_path = std::filesystem::path{args[it]};

      if (std::filesystem::is_directory(fs_path))
      {
        for (auto const &fs_entry: std::filesystem::recursive_directory_iterator{fs_path})
        {
          auto exclude = false;

          if (exclude_any_dir)
          {
            for (auto exclude_dir: exclude_dirs)
            {
              std::regex const exclude_dir_regex(exclude_dir);
              auto const entry = fs_entry.path().relative_path().string();
              if (std::regex_search(entry, exclude_dir_regex))
              {
                exclude = true;
              }
            }
          }

          {
            auto const entry = fs_entry.path().extension().string();
            if (!exclude && std::regex_search(entry, extension_regex))
            {
              filenames.push_back(fs_entry.path().string());
            }
          }
        }
      }
      else
      {
        if (std::filesystem::is_regular_file(fs_path))
        {
          auto const entry = fs_path.extension().string();
          if (std::regex_search(entry, extension_regex))
          {
            filenames.push_back(fs_path.string());
          }
        }
        else
        {
          auto const entry = fs_path.extension().string();
          if (std::regex_search(entry, extension_regex))
          {
            std::fstream new_file;
            new_file.open(fs_path.string(), std::ios_base::out);
            new_file.close();
            filenames.push_back(fs_path.string());
          }
        }
      }
    }
  }

  for (auto const &filename: filenames)
  {
    std::vector<std::string> lines;
    auto has_eof_newline = true;
    lines.reserve(10000);


    // read file
    {
      std::ifstream file;
      std::string line;

      file.open(filename);

      while (std::getline(file, line))
      {
        lines.push_back(line);
      }

      if (!lines.empty())
      {
        file.clear();
        file.seekg(-1, std::ios::end);
        has_eof_newline = (file.peek() == '\n');
      }

      file.close();
    }


    if (lines.empty())
    {
      lines.push_back("");
    }


    std::set<int> remove_lines;
    std::map<int, std::string> replace_lines;


    // find and replace
    bool is_modified = false;

    for (int it = 0; it != lines.size(); ++it)
    {
      std::string line = lines[it];
      if (find_and_replace(input_str.value(), output_str.value(), line))
      {
        is_modified = true;
        replace_lines[it] = line;
      }
    }


    // write modified file
    if (is_modified)
    {
      if (is_verbose)
      {
        std::cout << "  modified: " << filename << std::endl;
      }

      if (mv_file)
      {
        modfilenames.insert(filename);
      }

      std::ofstream file;
      file.open(filename);

      for (int it = 0; it != lines.size(); ++it)
      {
        auto default_action = true;

        if (default_action)
        {
          auto remove_line = remove_lines.find(it);
          if (remove_line != remove_lines.end())
          {
            default_action = false;
          }
        }

        if (default_action)
        {
          auto replace_line = replace_lines.find(it);
          if (replace_line != replace_lines.end())
          {
            file << replace_line->second << "\n";
            default_action = false;
          }
        }

        if (default_action)
        {
          if (it == lines.size() - 1)
          {
            if (has_eof_newline || fix_eof_newline)
              file << lines[it] << "\n";
            else
              file << lines[it];
          }
          else
          {
            file << lines[it] << "\n";
          }
        }
      }

      file.close();
    }

  } // file loop


  // rename modified files
  for (auto const &filename: modfilenames)
  {
    auto modfilename = filename;

    if (find_and_replace(input_str.value(), output_str.value(), modfilename))
    {
      if (is_verbose)
      {
        std::cout << "  renamed: " << filename << " to " << modfilename << std::endl;
      }

      std::filesystem::rename(filename, modfilename);
    }
  }
}
