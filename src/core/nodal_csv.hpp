#pragma once
// reader for nodal temperature results exported as csv with the columns
//   node_id,x_m,y_m,z_m,temperature_C
//
// this is the documented schema; a real ansys mechanical export needs its
// columns renamed/reordered to match (see data/PROVENANCE.md).

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wb::csv {

inline constexpr const char* kExpectedHeader = "node_id,x_m,y_m,z_m,temperature_C";

struct Limits {
    std::size_t max_bytes = 256u * 1024u * 1024u;
    std::size_t max_nodes = 5'000'000;
};

struct NodalField {
    std::vector<std::int64_t> node_id;
    std::vector<double> x_m, y_m, z_m, temp_C;
    double t_min_C = 0.0, t_max_C = 0.0;
    double bbox_min[3] = {0, 0, 0};
    double bbox_max[3] = {0, 0, 0};
    std::size_t blank_lines = 0;
    std::size_t size() const { return node_id.size(); }
};

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& what, std::size_t line, std::string hint)
        : std::runtime_error(what), line_(line), hint_(std::move(hint)) {}
    std::size_t line() const { return line_; }
    const std::string& hint() const { return hint_; }

private:
    std::size_t line_;
    std::string hint_;
};

NodalField parse_nodal(std::string_view text, const Limits& limits = {});

}  // namespace wb::csv
