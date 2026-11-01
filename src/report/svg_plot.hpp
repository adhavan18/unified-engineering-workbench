#pragma once
// writes simple, labelled line charts as standalone svg files (used for results/).

#include <string>
#include <vector>

namespace wb::report {

struct Series {
    std::string name;
    std::vector<double> x, y;
    std::string color = "#2563eb";
    bool dashed = false;
    bool markers = false;
    bool line = true;
};

struct Chart {
    std::string title;
    std::string x_label;  // include units, e.g. "x (m)"
    std::string y_label;
    bool log_x = false;
    bool log_y = false;
    std::vector<Series> series;
    std::vector<std::string> notes;  // small text lines under the title
};

// returns false if the file could not be written.
bool write_svg(const Chart& chart, const std::string& path, int width = 820, int height = 520);

}  // namespace wb::report
