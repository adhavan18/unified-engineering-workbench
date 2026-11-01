#include "report/svg_plot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>

namespace wb::report {

namespace {

std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

std::string num(double v) {
    char buf[32];
    if (v != 0 && (std::abs(v) < 1e-3 || std::abs(v) >= 1e5)) std::snprintf(buf, sizeof buf, "%.0e", v);
    else std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// "nice" linear ticks covering [lo, hi]
std::vector<double> linear_ticks(double lo, double hi) {
    const double span = hi - lo;
    const double raw = span / 6.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = mag;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        if (m * mag >= raw) { step = m * mag; break; }
    }
    std::vector<double> t;
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) t.push_back(std::abs(v) < step * 1e-9 ? 0 : v);
    return t;
}

std::vector<double> log_ticks(double lo, double hi) {
    std::vector<double> t;
    for (double e = std::floor(std::log10(lo)); e <= std::ceil(std::log10(hi)); e += 1) t.push_back(std::pow(10.0, e));
    return t;
}

}  // namespace

bool write_svg(const Chart& c, const std::string& path, int W, int H) {
    const double left = 86, right = 190, top = 58 + 16.0 * c.notes.size(), bottom = 64;
    const double pw = W - left - right, ph = H - top - bottom;

    double xlo = std::numeric_limits<double>::max(), xhi = std::numeric_limits<double>::lowest();
    double ylo = xlo, yhi = xhi;
    for (const auto& s : c.series) {
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if ((c.log_x && s.x[i] <= 0) || (c.log_y && s.y[i] <= 0)) continue;
            xlo = std::min(xlo, s.x[i]); xhi = std::max(xhi, s.x[i]);
            ylo = std::min(ylo, s.y[i]); yhi = std::max(yhi, s.y[i]);
        }
    }
    if (xlo > xhi) return false;
    if (c.log_x) { xlo = std::pow(10, std::floor(std::log10(xlo))); xhi = std::pow(10, std::ceil(std::log10(xhi))); }
    if (c.log_y) { ylo = std::pow(10, std::floor(std::log10(ylo))); yhi = std::pow(10, std::ceil(std::log10(yhi))); }
    if (!c.log_y) { const double pad = (yhi - ylo) * 0.06 + 1e-12; ylo -= pad; yhi += pad; }
    if (xhi == xlo) { xhi += 1; xlo -= 1; }

    auto X = [&](double v) {
        const double t = c.log_x ? (std::log10(v) - std::log10(xlo)) / (std::log10(xhi) - std::log10(xlo)) : (v - xlo) / (xhi - xlo);
        return left + t * pw;
    };
    auto Y = [&](double v) {
        const double t = c.log_y ? (std::log10(v) - std::log10(ylo)) / (std::log10(yhi) - std::log10(ylo)) : (v - ylo) / (yhi - ylo);
        return top + (1 - t) * ph;
    };

    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "\" height=\"" << H << "\" viewBox=\"0 0 " << W
      << ' ' << H << "\" font-family=\"Segoe UI, Arial, sans-serif\">\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    o << "<text x=\"" << left << "\" y=\"30\" font-size=\"17\" font-weight=\"600\" fill=\"#111827\">" << esc(c.title) << "</text>\n";
    for (std::size_t i = 0; i < c.notes.size(); ++i) {
        o << "<text x=\"" << left << "\" y=\"" << 50 + 16 * i << "\" font-size=\"12\" fill=\"#4b5563\">" << esc(c.notes[i]) << "</text>\n";
    }

    const auto xt = c.log_x ? log_ticks(xlo, xhi) : linear_ticks(xlo, xhi);
    const auto yt = c.log_y ? log_ticks(ylo, yhi) : linear_ticks(ylo, yhi);
    for (double v : xt) {
        if (v < xlo * (1 - 1e-9) || v > xhi * (1 + 1e-9) + 1e-12) continue;
        o << "<line x1=\"" << X(v) << "\" x2=\"" << X(v) << "\" y1=\"" << top << "\" y2=\"" << top + ph << "\" stroke=\"#e5e7eb\"/>\n";
        o << "<text x=\"" << X(v) << "\" y=\"" << top + ph + 18 << "\" font-size=\"12\" text-anchor=\"middle\" fill=\"#374151\">" << num(v) << "</text>\n";
    }
    for (double v : yt) {
        if (v < ylo - 1e-12 || v > yhi + 1e-12) continue;
        o << "<line x1=\"" << left << "\" x2=\"" << left + pw << "\" y1=\"" << Y(v) << "\" y2=\"" << Y(v) << "\" stroke=\"#e5e7eb\"/>\n";
        o << "<text x=\"" << left - 8 << "\" y=\"" << Y(v) + 4 << "\" font-size=\"12\" text-anchor=\"end\" fill=\"#374151\">" << num(v) << "</text>\n";
    }
    o << "<rect x=\"" << left << "\" y=\"" << top << "\" width=\"" << pw << "\" height=\"" << ph << "\" fill=\"none\" stroke=\"#9ca3af\"/>\n";
    o << "<text x=\"" << left + pw / 2 << "\" y=\"" << H - 18 << "\" font-size=\"13\" text-anchor=\"middle\" fill=\"#111827\">" << esc(c.x_label) << "</text>\n";
    o << "<text transform=\"translate(22," << top + ph / 2 << ") rotate(-90)\" font-size=\"13\" text-anchor=\"middle\" fill=\"#111827\">" << esc(c.y_label) << "</text>\n";

    for (const auto& s : c.series) {
        std::ostringstream pts;
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if ((c.log_x && s.x[i] <= 0) || (c.log_y && s.y[i] <= 0)) continue;
            pts << X(s.x[i]) << ',' << Y(s.y[i]) << ' ';
        }
        if (s.line) {
            o << "<polyline fill=\"none\" stroke=\"" << s.color << "\" stroke-width=\"2\"" << (s.dashed ? " stroke-dasharray=\"6 4\"" : "")
              << " points=\"" << pts.str() << "\"/>\n";
        }
        if (s.markers) {
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                if ((c.log_x && s.x[i] <= 0) || (c.log_y && s.y[i] <= 0)) continue;
                o << "<circle cx=\"" << X(s.x[i]) << "\" cy=\"" << Y(s.y[i]) << "\" r=\"3.5\" fill=\"" << s.color << "\"/>\n";
            }
        }
    }

    double ly = top + 6;
    for (const auto& s : c.series) {
        const double lx = left + pw + 16;
        o << "<line x1=\"" << lx << "\" x2=\"" << lx + 24 << "\" y1=\"" << ly + 6 << "\" y2=\"" << ly + 6 << "\" stroke=\"" << s.color
          << "\" stroke-width=\"2\"" << (s.dashed ? " stroke-dasharray=\"6 4\"" : "") << (s.line ? "" : " stroke-opacity=\"0\"") << "/>\n";
        if (s.markers) o << "<circle cx=\"" << lx + 12 << "\" cy=\"" << ly + 6 << "\" r=\"3.5\" fill=\"" << s.color << "\"/>\n";
        o << "<text x=\"" << lx + 30 << "\" y=\"" << ly + 10 << "\" font-size=\"12\" fill=\"#111827\">" << esc(s.name) << "</text>\n";
        ly += 20;
    }
    o << "</svg>\n";

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << o.str();
    return static_cast<bool>(f);
}

}  // namespace wb::report
