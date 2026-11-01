#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/stl.hpp"
#include "test_framework.hpp"

using wb::stl::ParseError;
using wb::stl::parse_ascii;

namespace {

const char* kTwoTriangles =
    "solid square\n"
    "  facet normal 0 0 1\n"
    "    outer loop\n"
    "      vertex 0 0 0\n"
    "      vertex 1 0 0\n"
    "      vertex 1 1 0\n"
    "    endloop\n"
    "  endfacet\n"
    "  facet normal 0 0 1\n"
    "    outer loop\n"
    "      vertex 0 0 0\n"
    "      vertex 1 1 0\n"
    "      vertex 0 1 2.5e-1\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid square\n";

std::string read_fixture(const char* name) {
    std::ifstream in(std::string("tests/fixtures/") + name, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string replace(std::string s, const std::string& from, const std::string& to) {
    s.replace(s.find(from), from.size(), to);
    return s;
}

}  // namespace

TEST(stl, parses_a_valid_ascii_file) {
    const auto m = parse_ascii(kTwoTriangles);
    CHECK(m.name == "square");
    CHECK(m.triangles == 2);
    CHECK(m.vertices.size() == 18);
    CHECK(m.normals.size() == 6);
    CHECK(m.degenerate == 0);
    CHECK_NEAR(m.bbox_max[2], 0.25, 1e-12);
    CHECK_NEAR(m.normals[2], 1.0, 1e-6);  // recomputed from the vertex winding
}

TEST(stl, accepts_crlf_tabs_uppercase_and_multiple_solids) {
    std::string crlf;
    for (char c : std::string(kTwoTriangles)) {
        if (c == '\n') crlf += "\r\n";
        else crlf += c;
    }
    const auto twice = replace(crlf, "facet normal", "FACET\tNORMAL") + crlf;
    const auto m = parse_ascii(twice);
    CHECK(m.triangles == 4);
    CHECK(m.solids == 2);
}

TEST(stl, parses_the_shipped_example) {
    const auto m = parse_ascii(read_fixture("../../data/examples/heatsink_ascii.stl"));
    CHECK(m.triangles == 96);  // base plate + 7 fins, 12 triangles per box
    CHECK(m.degenerate == 0);
}

TEST(stl, counts_degenerate_triangles) {
    const auto m = parse_ascii(replace(kTwoTriangles, "vertex 1 1 0\n    endloop", "vertex 2 0 0\n    endloop"));
    CHECK(m.degenerate == 1);
}

TEST(stl, rejects_empty_and_whitespace_files) {
    CHECK_THROWS_WITH(parse_ascii(""), ParseError, "empty");
    CHECK_THROWS_WITH(parse_ascii("  \n\t \n"), ParseError, "whitespace");
}

TEST(stl, rejects_files_without_triangles) {
    CHECK_THROWS_WITH(parse_ascii("solid nothing\nendsolid nothing\n"), ParseError, "no triangles");
}

TEST(stl, rejects_non_finite_coordinates_with_line_number) {
    try {
        parse_ascii(replace(kTwoTriangles, "vertex 1 0 0", "vertex 1 nan 0"));
        CHECK(false);
    } catch (const ParseError& e) {
        CHECK(std::string(e.what()).find("not finite") != std::string::npos);
        CHECK(e.line() == 5);
        CHECK(!e.hint().empty());
    }
    CHECK_THROWS_WITH(parse_ascii(replace(kTwoTriangles, "vertex 1 0 0", "vertex 1 inf 0")), ParseError, "not finite");
    CHECK_THROWS_WITH(parse_ascii(replace(kTwoTriangles, "vertex 1 0 0", "vertex 1 1e999 0")), ParseError,
                      "not finite");
}

TEST(stl, rejects_malformed_numbers_and_structure) {
    CHECK_THROWS_WITH(parse_ascii(replace(kTwoTriangles, "vertex 1 0 0", "vertex 1 abc 0")), ParseError,
                      "not a number");
    CHECK_THROWS_WITH(parse_ascii(replace(kTwoTriangles, "      vertex 1 0 0\n", "")), ParseError, "2 vertices");
    CHECK_THROWS_WITH(parse_ascii(replace(kTwoTriangles, "      vertex 1 0 0\n", "      vertex 1 0 0\n      vertex 5 5 5\n")),
                      ParseError, "more than 3");
    CHECK_THROWS_WITH(parse_ascii(replace(kTwoTriangles, "outer loop", "outer")), ParseError, "expected 'loop'");
    CHECK_THROWS_WITH(parse_ascii("hello world\n"), ParseError, "expected 'solid'");
}

TEST(stl, rejects_truncated_files) {
    const std::string full = kTwoTriangles;
    CHECK_THROWS_WITH(parse_ascii(full.substr(0, full.find("endsolid"))), ParseError, "endsolid");
    CHECK_THROWS_WITH(parse_ascii(full.substr(0, full.find("vertex 1 0 0") + 9)), ParseError, "ends early");
}

TEST(stl, detects_binary_stl) {
    std::string bin(84 + 50, '\0');
    std::memcpy(&bin[0], "binary header", 13);
    const std::uint32_t one = 1;
    std::memcpy(&bin[80], &one, 4);
    CHECK(wb::stl::looks_binary(bin));
    CHECK_THROWS_WITH(parse_ascii(bin), ParseError, "binary stl");

    // some exporters start binary headers with "solid", which fools naive readers
    std::memcpy(&bin[0], "solid trap   ", 13);
    CHECK_THROWS_WITH(parse_ascii(bin), ParseError, "binary");
}

TEST(stl, enforces_size_limits) {
    wb::stl::Limits tiny;
    tiny.max_triangles = 1;
    CHECK_THROWS_WITH(parse_ascii(kTwoTriangles, tiny), ParseError, "more than 1 triangles");
    tiny = {};
    tiny.max_bytes = 10;
    CHECK_THROWS_WITH(parse_ascii(kTwoTriangles, tiny), ParseError, "limit");
}

TEST(stl, malformed_fixture_files_are_rejected) {
    for (const char* f : {"bad_nan_vertex.stl", "bad_truncated.stl", "bad_two_vertices.stl", "empty.stl"}) {
        bool threw = false;
        try {
            parse_ascii(read_fixture(f));
        } catch (const ParseError&) {
            threw = true;
        }
        CHECK(threw);
    }
}
