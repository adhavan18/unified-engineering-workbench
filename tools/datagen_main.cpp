// generates the self-created example data in data/examples, and large files
// for the documented file-size tests.
//
//   wb_datagen examples <out_dir>
//   wb_datagen big-stl <file> <boxes>        many boxes -> 12 * boxes triangles
//   wb_datagen big-csv <file> <nodes>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "core/fea1d.hpp"

namespace {

void facet(std::ofstream& out, const double a[3], const double b[3], const double c[3]) {
    // the normal is written for completeness; readers (including ours) recompute it
    const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const double v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (double& x : n) x /= len;
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "  facet normal %g %g %g\n    outer loop\n      vertex %g %g %g\n      vertex %g %g %g\n"
                  "      vertex %g %g %g\n    endloop\n  endfacet\n",
                  n[0], n[1], n[2], a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    out << buf;
}

// axis-aligned box as 12 outward-facing triangles (counter-clockwise from outside)
void box(std::ofstream& out, double x0, double y0, double z0, double x1, double y1, double z1) {
    const double p[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                            {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    const int quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
    for (const auto& q : quads) {
        facet(out, p[q[0]], p[q[1]], p[q[2]]);
        facet(out, p[q[0]], p[q[2]], p[q[3]]);
    }
}

// a finned heat sink in millimetres: 60 x 40 x 5 base with 7 fins 2 mm thick
// and 25 mm tall. fins sit on the base as separate closed boxes, which is
// common in simple exports and fine for visualisation.
void write_heatsink(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    out << "solid heatsink_60x40mm_self_created\n";
    box(out, 0, 0, 0, 60, 40, 5);
    for (int i = 0; i < 7; ++i) {
        const double x = 2.0 + i * 9.0;
        box(out, x, 0, 5, x + 2.0, 40, 30);
    }
    out << "endsolid heatsink_60x40mm_self_created\n";
}

// synthetic nodal results: the task c rod (1 m long, 10 mm x 10 mm section)
// sampled on a 3d grid of nodes with the analytical temperature. lateral faces
// are insulated, so temperature is uniform over each cross-section.
void write_rod_csv(const std::string& path) {
    const wb::fea::RodProblem rod;
    std::ofstream out(path, std::ios::binary);
    out << "node_id,x_m,y_m,z_m,temperature_C\n";
    int id = 1;
    const int stations = 81, across = 5;
    for (int i = 0; i < stations; ++i) {
        const double x = rod.length_m * i / (stations - 1);
        const double t = wb::fea::exact_temperature(rod, x);
        for (int j = 0; j < across; ++j) {
            for (int k = 0; k < across; ++k) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "%d,%.6f,%.6f,%.6f,%.6f\n", id++, x, -0.005 + 0.01 * j / (across - 1),
                              -0.005 + 0.01 * k / (across - 1), t);
                out << buf;
            }
        }
    }
}

void write_big_stl(const std::string& path, long boxes) {
    std::ofstream out(path, std::ios::binary);
    out << "solid size_test\n";
    const long side = static_cast<long>(std::ceil(std::sqrt(static_cast<double>(boxes))));
    for (long b = 0; b < boxes; ++b) {
        const double x = static_cast<double>(b % side) * 2.0, y = static_cast<double>(b / side) * 2.0;
        box(out, x, y, 0, x + 1, y + 1, 1 + (b % 7) * 0.25);
    }
    out << "endsolid size_test\n";
}

void write_big_csv(const std::string& path, long nodes) {
    std::ofstream out(path, std::ios::binary);
    out << "node_id,x_m,y_m,z_m,temperature_C\n";
    const long side = static_cast<long>(std::ceil(std::cbrt(static_cast<double>(nodes))));
    for (long i = 0; i < nodes; ++i) {
        const double x = static_cast<double>(i % side) / side;
        const double y = static_cast<double>((i / side) % side) / side;
        const double z = static_cast<double>(i / (side * side)) / side;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%ld,%.6f,%.6f,%.6f,%.4f\n", i + 1, x, y, z, 20.0 + 80.0 * x * y + 10.0 * z);
        out << buf;
    }
}

int usage() {
    std::fprintf(stderr, "usage: wb_datagen examples <dir> | big-stl <file> <boxes> | big-csv <file> <nodes>\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    if (cmd == "examples") {
        const std::string dir = argv[2];
        write_heatsink(dir + "/heatsink_ascii.stl");
        write_rod_csv(dir + "/rod_nodal_temperatures_synthetic.csv");
        std::printf("wrote %s/heatsink_ascii.stl and rod_nodal_temperatures_synthetic.csv\n", dir.c_str());
        return 0;
    }
    if (argc < 4) return usage();
    if (cmd == "big-stl") { write_big_stl(argv[2], std::atol(argv[3])); return 0; }
    if (cmd == "big-csv") { write_big_csv(argv[2], std::atol(argv[3])); return 0; }
    return usage();
}
