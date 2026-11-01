#include <fstream>
#include <sstream>

#include "core/nodal_csv.hpp"
#include "test_framework.hpp"

using wb::csv::ParseError;
using wb::csv::parse_nodal;

namespace {

const char* kValid =
    "node_id,x_m,y_m,z_m,temperature_C\n"
    "1,0,0,0,100\n"
    "2,0.5,0,0,162.5\n"
    "3,1.0,0,0,20\n";

std::string read_fixture(const char* name) {
    std::ifstream in(std::string("tests/fixtures/") + name, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST(csv, parses_a_valid_file) {
    const auto f = parse_nodal(kValid);
    CHECK(f.size() == 3);
    CHECK(f.node_id[1] == 2);
    CHECK_NEAR(f.x_m[1], 0.5, 0.0);
    CHECK_NEAR(f.t_min_C, 20.0, 0.0);
    CHECK_NEAR(f.t_max_C, 162.5, 0.0);
    CHECK_NEAR(f.bbox_max[0], 1.0, 0.0);
}

TEST(csv, accepts_bom_crlf_spaces_and_trailing_blank_lines) {
    const std::string text = "\xEF\xBB\xBFnode_id, x_m ,y_m,z_m,temperature_C\r\n1, 0 ,0,0, 25.5\r\n\r\n";
    const auto f = parse_nodal(text);
    CHECK(f.size() == 1);
    CHECK_NEAR(f.temp_C[0], 25.5, 0.0);
    CHECK(f.blank_lines == 1);
}

TEST(csv, parses_the_shipped_synthetic_example) {
    const auto f = parse_nodal(read_fixture("../../data/examples/rod_nodal_temperatures_synthetic.csv"));
    CHECK(f.size() > 100);
    CHECK(f.t_max_C > 180.0 && f.t_max_C < 190.0);  // peak of the task c rod is 188.2 C
}

TEST(csv, explains_empty_and_header_only_files) {
    CHECK_THROWS_WITH(parse_nodal(""), ParseError, "empty");
    CHECK_THROWS_WITH(parse_nodal("\n \n"), ParseError, "whitespace");
    CHECK_THROWS_WITH(parse_nodal("node_id,x_m,y_m,z_m,temperature_C\n"), ParseError, "no data rows");
}

TEST(csv, rejects_wrong_headers) {
    CHECK_THROWS_WITH(parse_nodal("id,x,y,z,t\n1,0,0,0,1\n"), ParseError, "expected 'node_id'");
    CHECK_THROWS_WITH(parse_nodal("node_id,x_m,y_m,z_m\n1,0,0,0\n"), ParseError, "4 columns");
    CHECK_THROWS_WITH(parse_nodal("node_id,x_m,y_m,z_m,temperature_K\n1,0,0,0,1\n"), ParseError, "temperature_C");
}

TEST(csv, rejects_bad_rows_with_line_numbers) {
    const std::string head = "node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,10\n";
    try {
        parse_nodal(head + "2,0,0,0,nan\n");
        CHECK(false);
    } catch (const ParseError& e) {
        CHECK(std::string(e.what()).find("not finite") != std::string::npos);
        CHECK(e.line() == 3);
    }
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,0,0,inf\n"), ParseError, "not finite");
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,0,0\n"), ParseError, "4 values");
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,0,0,1,9\n"), ParseError, "6 values");
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,abc,0,1\n"), ParseError, "y_m");
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,0,0,1,5\n"), ParseError, "values");
    CHECK_THROWS_WITH(parse_nodal(head + "2.5,0,0,0,1\n"), ParseError, "whole number");
    CHECK_THROWS_WITH(parse_nodal(head + "1,0,0,0,1\n"), ParseError, "appears twice");
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,0,0,-300\n"), ParseError, "absolute zero");
    CHECK_THROWS_WITH(parse_nodal(head + "2,\"0\",0,0,1\n"), ParseError, "quoted");
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,0,0,1,\n"), ParseError, "6 values");
    CHECK_THROWS_WITH(parse_nodal(head + "2,0,0,0,\n"), ParseError, "not a number");
}

TEST(csv, enforces_limits) {
    wb::csv::Limits lim;
    lim.max_nodes = 2;
    CHECK_THROWS_WITH(parse_nodal(kValid, lim), ParseError, "more than 2 nodes");
}

TEST(csv, malformed_fixture_files_are_rejected) {
    for (const char* f : {"bad_header.csv", "bad_nan_temperature.csv", "bad_missing_column.csv", "empty.csv",
                          "header_only.csv"}) {
        bool threw = false;
        try {
            parse_nodal(read_fixture(f));
        } catch (const ParseError&) {
            threw = true;
        }
        CHECK(threw);
    }
}
