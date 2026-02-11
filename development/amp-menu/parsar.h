#ifndef __PARSAR_H__
#define __PARSAR_H__

#include <string>
#include <vector>

struct ExecArgs {
    std::vector<std::string> storage; // owns the strings
    std::vector<char *> argv;         // pointers into storage; argv.back()==nullptr
};

ExecArgs tokenize_and_expand_argv(const std::string &s);

#endif // !__PARSAR_H__
