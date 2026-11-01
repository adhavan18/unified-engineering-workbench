#pragma once
// tiny self-registering test harness (no external test library needed).

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace wbtest {

struct Case {
    const char* suite;
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Failure {
    std::string message;
};

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back({suite, name, std::move(fn)});
    }
};

inline std::string where(const char* file, int line) {
    std::ostringstream ss;
    ss << file << ":" << line;
    return ss.str();
}

}  // namespace wbtest

#define WB_CAT2(a, b) a##b
#define WB_CAT(a, b) WB_CAT2(a, b)

#define TEST(suite, name)                                                                        \
    static void WB_CAT(test_, WB_CAT(suite, WB_CAT(_, name)))();                                 \
    static wbtest::Registrar WB_CAT(reg_, WB_CAT(suite, WB_CAT(_, name)))(                       \
        #suite, #name, WB_CAT(test_, WB_CAT(suite, WB_CAT(_, name))));                           \
    static void WB_CAT(test_, WB_CAT(suite, WB_CAT(_, name)))()

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) throw wbtest::Failure{wbtest::where(__FILE__, __LINE__) + ": CHECK(" #cond ")"}; \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                    \
    do {                                                                                         \
        const double va_ = (a), vb_ = (b);                                                       \
        if (!(std::abs(va_ - vb_) <= (tol))) {                                                   \
            std::ostringstream ss_;                                                              \
            ss_.precision(17);                                                                   \
            ss_ << wbtest::where(__FILE__, __LINE__) << ": " #a " = " << va_ << ", " #b " = " << vb_ \
                << ", |diff| = " << std::abs(va_ - vb_) << " > " << (tol);                       \
            throw wbtest::Failure{ss_.str()};                                                    \
        }                                                                                        \
    } while (0)

// passes when `expr` throws `type`; the exception message must contain `needle`.
#define CHECK_THROWS_WITH(expr, type, needle)                                                    \
    do {                                                                                         \
        bool thrown_ = false;                                                                    \
        try {                                                                                    \
            (void)(expr);                                                                        \
        } catch (const type& e_) {                                                               \
            thrown_ = true;                                                                      \
            if (std::string(e_.what()).find(needle) == std::string::npos) {                      \
                throw wbtest::Failure{wbtest::where(__FILE__, __LINE__) + ": message '" + e_.what() + \
                                      "' does not contain '" + needle + "'"};                    \
            }                                                                                    \
        }                                                                                        \
        if (!thrown_) throw wbtest::Failure{wbtest::where(__FILE__, __LINE__) + ": expected " #type}; \
    } while (0)
