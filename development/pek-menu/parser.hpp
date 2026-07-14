/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __PARSER_HPP__
#define __PARSER_HPP__

#include <string>
#include <vector>

struct ExecArgs {
    std::vector<std::string> storage; // owns the strings
    std::vector<char *> argv;         // pointers into storage; argv.back()==nullptr
};

ExecArgs tokenize_and_expand_argv(const std::string &s);
std::vector<std::string> extract_pekinfer_opchain_paths(const ExecArgs &args);
std::string trim(std::string trimmed_str);

#endif // !__PARSER_HPP__
