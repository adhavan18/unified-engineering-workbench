#include "core/stl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "core/text_scan.hpp"

namespace wb::stl {

namespace {

// whitespace tokenizer that remembers the line of the current token.
class Tokens {
public:
    explicit Tokens(std::string_view text) : text_(text) {}

    // next token, or empty at end of input
    std::string_view next() {
        skip_space();
        token_line_ = line_;
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !text::is_space(text_[pos_])) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    // the rest of the current line (used for the optional solid name)
    std::string_view rest_of_line() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
        return text::trim(text_.substr(start, pos_ - start));
    }

    bool at_end() {
        skip_space();
        return pos_ >= text_.size();
    }

    std::size_t line() const { return token_line_; }

private:
    void skip_space() {
        while (pos_ < text_.size() && text::is_space(text_[pos_])) {
            if (text_[pos_] == '\n') ++line_;
            ++pos_;
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t token_line_ = 1;
};

const char* kAsciiHint =
    "expected ascii stl: solid / facet normal / outer loop / 3 x vertex / endloop / endfacet / endsolid";

void expect(Tokens& t, std::string_view keyword) {
    const std::string_view tok = t.next();
    if (tok.empty()) {
        throw ParseError("file ends early: expected '" + std::string(keyword) + "'", t.line(),
                         "the file looks truncated. re-export it from the cad tool");
    }
    if (text::lower(tok) != keyword) {
        throw ParseError("expected '" + std::string(keyword) + "' but found '" + text::preview(tok) + "'",
                         t.line(), kAsciiHint);
    }
}

double read_number(Tokens& t, const char* what) {
    const std::string_view tok = t.next();
    double v = 0.0;
    switch (text::parse_double(tok, v)) {
        case text::NumberStatus::ok:
            return v;
        case text::NumberStatus::non_finite:
            throw ParseError(std::string(what) + " is not finite ('" + text::preview(tok) + "')", t.line(),
                             "nan or infinite coordinates usually mean a broken export; fix the model and re-export");
        default:
            if (tok.empty()) {
                throw ParseError(std::string("file ends early while reading ") + what, t.line(),
                                 "the file looks truncated. re-export it from the cad tool");
            }
            throw ParseError(std::string(what) + " is not a number ('" + text::preview(tok) + "')", t.line(),
                             "each vertex line needs exactly three numbers, e.g. 'vertex 0 1.5 -2e-3'");
    }
}

void face_normal(const float* v, float* n) {
    const float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2];
    const float bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
    n[0] = ay * bz - az * by;
    n[1] = az * bx - ax * bz;
    n[2] = ax * by - ay * bx;
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 0.0f) {
        n[0] /= len; n[1] /= len; n[2] /= len;
    }
}

}  // namespace

bool looks_binary(std::string_view bytes) {
    if (bytes.size() < 84) return false;
    std::uint32_t count = 0;
    std::memcpy(&count, bytes.data() + 80, sizeof count);  // little-endian on x86/x64
    return 84ull + 50ull * count == bytes.size();
}

Mesh parse_ascii(std::string_view text, const Limits& limits) {
    if (text.empty()) {
        throw ParseError("the file is empty (0 bytes)", 0, "choose a non-empty ascii .stl file");
    }
    if (text.size() > limits.max_bytes) {
        throw ParseError("the file is larger than the " + std::to_string(limits.max_bytes / (1024 * 1024)) +
                             " MB limit",
                         0, "decimate the mesh in the cad tool or split it into parts");
    }
    if (text::trim(text).empty()) {
        throw ParseError("the file only contains whitespace", 0, "choose a non-empty ascii .stl file");
    }
    if (looks_binary(text) && text::lower(text::trim(text).substr(0, 5)) != "solid") {
        throw ParseError("this is a binary stl", 0, "only ascii stl is supported: re-export with 'ascii' selected");
    }
    for (std::size_t i = 0; i < std::min<std::size_t>(text.size(), 4096); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0 || (c < 0x20 && !text::is_space(static_cast<char>(c)))) {
            throw ParseError(looks_binary(text) ? "this is a binary stl whose header starts with 'solid'"
                                                : "the file contains binary (non-text) bytes",
                             0, "only ascii stl is supported: re-export with 'ascii' selected");
        }
    }

    Mesh mesh;
    Tokens t(text);
    double lo[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::max()};
    double hi[3] = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                    std::numeric_limits<double>::lowest()};

    while (!t.at_end()) {
        expect(t, "solid");
        const std::string_view name = t.rest_of_line();
        if (mesh.solids == 0) mesh.name = std::string(name);
        ++mesh.solids;

        for (;;) {
            const std::string_view tok = t.next();
            const std::string kw = text::lower(tok);
            if (kw == "endsolid") {
                t.rest_of_line();  // optional trailing name
                break;
            }
            if (kw.empty()) {
                throw ParseError("file ends before 'endsolid'", t.line(),
                                 "the file looks truncated. re-export it from the cad tool");
            }
            if (kw != "facet") {
                throw ParseError("expected 'facet' or 'endsolid' but found '" + text::preview(tok) + "'", t.line(),
                                 kAsciiHint);
            }
            if (mesh.triangles >= limits.max_triangles) {
                throw ParseError("more than " + std::to_string(limits.max_triangles) + " triangles", t.line(),
                                 "decimate the mesh in the cad tool before importing");
            }
            expect(t, "normal");
            for (int i = 0; i < 3; ++i) read_number(t, "facet normal component");
            expect(t, "outer");
            expect(t, "loop");

            float v[9];
            for (int k = 0; k < 3; ++k) {
                const std::string_view vt = t.next();
                if (text::lower(vt) != "vertex") {
                    throw ParseError("facet has " + std::to_string(k) + " vertices; expected 3 (found '" +
                                         text::preview(vt) + "')",
                                     t.line(), "every facet must be a triangle with exactly three 'vertex' lines");
                }
                for (int a = 0; a < 3; ++a) {
                    const double c = read_number(t, "vertex coordinate");
                    if (std::abs(c) > 3.0e38) {
                        throw ParseError("vertex coordinate is outside the float range", t.line(),
                                         "check the export units; values this large are not a real part");
                    }
                    v[3 * k + a] = static_cast<float>(c);
                    lo[a] = std::min(lo[a], c);
                    hi[a] = std::max(hi[a], c);
                }
            }
            const std::string_view after = t.next();
            if (text::lower(after) == "vertex") {
                throw ParseError("facet has more than 3 vertices", t.line(),
                                 "every facet must be a triangle; triangulate polygons before export");
            }
            if (text::lower(after) != "endloop") {
                throw ParseError("expected 'endloop' but found '" + text::preview(after) + "'", t.line(), kAsciiHint);
            }
            expect(t, "endfacet");

            float n[3];
            face_normal(v, n);
            if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) ++mesh.degenerate;
            mesh.vertices.insert(mesh.vertices.end(), v, v + 9);
            mesh.normals.insert(mesh.normals.end(), n, n + 3);
            ++mesh.triangles;
        }
    }

    if (mesh.triangles == 0) {
        throw ParseError("the stl is well-formed but contains no triangles", 0,
                         "the export produced an empty solid; check that a body was selected");
    }
    for (int a = 0; a < 3; ++a) {
        mesh.bbox_min[a] = lo[a];
        mesh.bbox_max[a] = hi[a];
    }
    return mesh;
}

}  // namespace wb::stl
