#include "string_util.hpp"

namespace duckdb {

string EncodeSpaces(const string &url) {
    string out;
    out.reserve(url.size());
    for (char c : url) {
        out += (c == ' ') ? "%20" : string(1, c);
    }
    return out;
}

}  // namespace duckdb
