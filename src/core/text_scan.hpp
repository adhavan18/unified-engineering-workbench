#pragma once
// small helpers shared by the stl and csv readers.

#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace wb::text {

inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

enum class NumberStatus { ok, not_a_number, non_finite };

// parses the whole token as a double. "nan"/"inf" parse but are reported as
// non-finite so callers can give a precise message.
inline NumberStatus parse_double(std::string_view token, double& out) {
    if (!token.empty() && token.front() == '+') token.remove_prefix(1);
    if (token.empty()) return NumberStatus::not_a_number;
    const char* first = token.data();
    const char* last = first + token.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    if (ec == std::errc::result_out_of_range) return NumberStatus::non_finite;
    if (ec != std::errc() || ptr != last) return NumberStatus::not_a_number;
    return std::isfinite(out) ? NumberStatus::ok : NumberStatus::non_finite;
}

// first 40 chars of a token for error messages, with control bytes made visible.
inline std::string preview(std::string_view s) {
    std::string out;
    for (char c : s.substr(0, 40)) out += (static_cast<unsigned char>(c) < 0x20) ? '?' : c;
    if (s.size() > 40) out += "...";
    return out;
}

}  // namespace wb::text
