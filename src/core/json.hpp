#pragma once
// minimal streaming json writer used by the transport layer and the report tool.
// it only writes json; the backend never needs to parse it (requests use query
// strings and raw file bodies).

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace wb {

class JsonWriter {
public:
    JsonWriter& begin_object() { prefix(); out_ += '{'; first_.push_back(true); return *this; }
    JsonWriter& end_object() { out_ += '}'; first_.pop_back(); return *this; }
    JsonWriter& begin_array() { prefix(); out_ += '['; first_.push_back(true); return *this; }
    JsonWriter& end_array() { out_ += ']'; first_.pop_back(); return *this; }

    JsonWriter& key(std::string_view k) {
        prefix();
        write_string(k);
        out_ += ':';
        after_key_ = true;
        return *this;
    }

    JsonWriter& value(std::string_view s) { prefix(); write_string(s); return *this; }
    JsonWriter& value(const std::string& s) { return value(std::string_view(s)); }
    JsonWriter& value(const char* s) { return value(std::string_view(s)); }
    JsonWriter& value(bool b) { prefix(); out_ += b ? "true" : "false"; return *this; }
    JsonWriter& value(double d) {
        prefix();
        if (!std::isfinite(d)) { out_ += "null"; return *this; }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.15g", d);
        out_ += buf;
        return *this;
    }
    template <class T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    JsonWriter& value(T v) { prefix(); out_ += std::to_string(v); return *this; }

    JsonWriter& null() { prefix(); out_ += "null"; return *this; }

    // splices an already-serialised json value (e.g. a cached sub-document).
    JsonWriter& raw(std::string_view json) { prefix(); out_ += json; return *this; }

    template <class T>
    JsonWriter& field(std::string_view k, const T& v) { key(k); return value(v); }

    JsonWriter& array(const std::vector<double>& values) {
        begin_array();
        for (double v : values) value(v);
        return end_array();
    }

    const std::string& str() const { return out_; }

    static std::string escape(std::string_view s) {
        JsonWriter w;
        w.write_string(s);
        return w.out_;
    }

private:
    void prefix() {
        if (after_key_) { after_key_ = false; return; }
        if (!first_.empty()) {
            if (!first_.back()) out_ += ',';
            first_.back() = false;
        }
    }

    void write_string(std::string_view s) {
        out_ += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out_ += buf;
                    } else {
                        out_ += static_cast<char>(c);
                    }
            }
        }
        out_ += '"';
    }

    std::string out_;
    std::vector<bool> first_;
    bool after_key_ = false;
};

}  // namespace wb
