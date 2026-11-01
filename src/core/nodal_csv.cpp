#include "core/nodal_csv.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <unordered_set>

#include "core/fea1d.hpp"
#include "core/text_scan.hpp"

namespace wb::csv {

namespace {

constexpr std::size_t kColumns = 5;
const char* kColumnNames[kColumns] = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};

std::vector<std::string_view> split_commas(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = line.find(',', start);
        fields.push_back(text::trim(line.substr(start, comma - start)));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return fields;
}

void check_header(std::string_view line) {
    const auto fields = split_commas(line);
    const std::string hint = std::string("the first line must be exactly: ") + kExpectedHeader;
    if (fields.size() != kColumns) {
        throw ParseError("header has " + std::to_string(fields.size()) + " columns; expected 5", 1, hint);
    }
    for (std::size_t i = 0; i < kColumns; ++i) {
        if (text::lower(fields[i]) != text::lower(kColumnNames[i])) {
            throw ParseError("header column " + std::to_string(i + 1) + " is '" + text::preview(fields[i]) +
                                 "'; expected '" + kColumnNames[i] + "'",
                             1, hint + " (units are fixed: metres and degrees celsius)");
        }
    }
}

}  // namespace

NodalField parse_nodal(std::string_view text, const Limits& limits) {
    if (text.empty()) {
        throw ParseError("the file is empty (0 bytes)", 0,
                         std::string("a results csv needs a header line: ") + kExpectedHeader);
    }
    if (text.size() > limits.max_bytes) {
        throw ParseError("the file is larger than the " + std::to_string(limits.max_bytes / (1024 * 1024)) +
                             " MB limit",
                         0, "export a subset of nodes (e.g. one named selection)");
    }
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);  // utf-8 byte-order mark written by excel
    }
    if (text::trim(text).empty()) {
        throw ParseError("the file only contains whitespace", 0,
                         std::string("a results csv needs a header line: ") + kExpectedHeader);
    }

    NodalField f;
    std::unordered_set<std::int64_t> seen;
    std::size_t line_no = 0;
    std::size_t pos = 0;
    bool header_done = false;

    // `pos < size` (not <=): a final '\n' terminates the last line, it does not start a new one
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if (text::trim(line).empty()) {
            if (header_done) ++f.blank_lines;
            continue;
        }
        if (!header_done) {
            check_header(line);
            header_done = true;
            continue;
        }
        if (line.find('"') != std::string_view::npos) {
            throw ParseError("quoted fields are not supported", line_no,
                             "save the csv without quotes; all five values are plain numbers");
        }

        const auto fields = split_commas(line);
        if (fields.size() != kColumns) {
            throw ParseError("row has " + std::to_string(fields.size()) + " values; expected 5", line_no,
                             std::string("each row needs ") + kExpectedHeader);
        }
        if (f.size() >= limits.max_nodes) {
            throw ParseError("more than " + std::to_string(limits.max_nodes) + " nodes", line_no,
                             "export a subset of nodes (e.g. one named selection)");
        }

        std::int64_t id = 0;
        const auto idf = fields[0];
        auto [ptr, ec] = std::from_chars(idf.data(), idf.data() + idf.size(), id);
        if (ec != std::errc() || ptr != idf.data() + idf.size() || idf.empty()) {
            throw ParseError("node_id '" + text::preview(idf) + "' is not a whole number", line_no,
                             "node ids must be integers such as 1, 2, 3");
        }
        if (id < 0) throw ParseError("node_id " + std::to_string(id) + " is negative", line_no, "node ids must be >= 0");
        if (!seen.insert(id).second) {
            throw ParseError("node_id " + std::to_string(id) + " appears twice", line_no,
                             "each node may appear once; remove duplicate rows");
        }

        double v[4];
        for (std::size_t c = 1; c < kColumns; ++c) {
            switch (text::parse_double(fields[c], v[c - 1])) {
                case text::NumberStatus::ok:
                    break;
                case text::NumberStatus::non_finite:
                    throw ParseError(std::string(kColumnNames[c]) + " is not finite ('" + text::preview(fields[c]) +
                                         "')",
                                     line_no, "nan/inf values usually mean an unconverged or masked result; remove them");
                default:
                    throw ParseError(std::string(kColumnNames[c]) + " '" + text::preview(fields[c]) +
                                         "' is not a number",
                                     line_no, "use plain decimal numbers with a '.' decimal point, e.g. 0.125");
            }
        }
        if (v[3] < fea::kAbsoluteZero_C) {
            throw ParseError("temperature " + std::to_string(v[3]) + " C is below absolute zero", line_no,
                             "check that the column is in degrees celsius, not kelvin or fahrenheit offsets");
        }

        f.node_id.push_back(id);
        f.x_m.push_back(v[0]);
        f.y_m.push_back(v[1]);
        f.z_m.push_back(v[2]);
        f.temp_C.push_back(v[3]);
    }

    if (f.size() == 0) {
        throw ParseError("the header is valid but there are no data rows", 0,
                         "the export contains no nodes; check the result object was evaluated before exporting");
    }

    f.t_min_C = *std::min_element(f.temp_C.begin(), f.temp_C.end());
    f.t_max_C = *std::max_element(f.temp_C.begin(), f.temp_C.end());
    const std::vector<double>* axes[3] = {&f.x_m, &f.y_m, &f.z_m};
    for (int a = 0; a < 3; ++a) {
        auto [lo, hi] = std::minmax_element(axes[a]->begin(), axes[a]->end());
        f.bbox_min[a] = *lo;
        f.bbox_max[a] = *hi;
    }
    return f;
}

}  // namespace wb::csv
