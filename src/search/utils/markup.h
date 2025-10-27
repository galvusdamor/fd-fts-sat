#ifndef UTILS_MARKUP_H
#define UTILS_MARKUP_H

#include <string>
#include <vector>


#define INTPAD 4
#define PATHPAD 15
#define STRINGPAD 0

namespace utils {
extern std::string format_paper_reference(
    const std::vector<std::string> &authors, const std::string &title,
    const std::string &url, const std::string &conference,
    const std::string &pages, const std::string &publisher);

std::string path_string(const std::vector<int> & path);
std::string path_string_no_sep(const std::vector<int> & path);
std::string pad_string(std::string s, int chars = STRINGPAD);
std::string pad_int(int i, int chars = INTPAD);
std::string pad_path(const std::vector<int> & path, int chars = PATHPAD);

}

#endif
