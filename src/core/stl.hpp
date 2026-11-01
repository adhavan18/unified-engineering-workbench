#pragma once
// ascii stl reader with strict validation.
//
// stl carries no unit; the caller chooses one for display. binary stl is
// detected and rejected with an explanation (only ascii is in scope).

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wb::stl {

struct Limits {
    std::size_t max_bytes = 256u * 1024u * 1024u;
    std::size_t max_triangles = 2'000'000;
};

struct Mesh {
    std::string name;
    std::vector<float> vertices;  // 9 floats per triangle
    std::vector<float> normals;   // 3 floats per triangle, recomputed from the vertices
    std::size_t triangles = 0;
    std::size_t degenerate = 0;   // zero-area triangles (kept, but reported)
    std::size_t solids = 0;
    double bbox_min[3] = {0, 0, 0};
    double bbox_max[3] = {0, 0, 0};
};

// error with the 1-based line where parsing stopped (0 when not line-specific)
// and a hint on how to fix the file.
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

Mesh parse_ascii(std::string_view text, const Limits& limits = {});

// true when the bytes look like a binary stl (size matches 84 + 50 * count).
bool looks_binary(std::string_view bytes);

}  // namespace wb::stl
