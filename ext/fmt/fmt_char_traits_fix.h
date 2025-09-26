#pragma once

// minimal includes so std::streamoff/streampos, size_t and EOF are available
#include <cstddef>
#include <ios>
#include <cstdio>

// Forward-declare fmt::char8_t as an enum to match fmt's definition.
namespace fmt {
inline namespace v5 { enum char8_t : unsigned char; }
}

namespace std {
template<>
struct char_traits<fmt::v5::char8_t> {
  using char_type = fmt::v5::char8_t;
  using int_type = int;
  using off_type = streamoff;
  using pos_type = streampos;

  static void assign(char_type& r, const char_type& a) noexcept { r = a; }
  static bool eq(char_type a, char_type b) noexcept { return a == b; }
  static bool lt(char_type a, char_type b) noexcept { return a < b; }
  static int compare(const char_type* s1, const char_type* s2, size_t n) noexcept {
    return std::char_traits<char>::compare(
      reinterpret_cast<const char*>(s1),
      reinterpret_cast<const char*>(s2), n);
  }
  static size_t length(const char_type* s) noexcept {
    return std::char_traits<char>::length(reinterpret_cast<const char*>(s));
  }
  static char_type* copy(char_type* dest, const char_type* src, size_t n) {
    return reinterpret_cast<char_type*>(
      std::char_traits<char>::copy(reinterpret_cast<char*>(dest),
                                   reinterpret_cast<const char*>(src), n));
  }
  static char_type* move(char_type* dest, const char_type* src, size_t n) {
    return reinterpret_cast<char_type*>(
      std::char_traits<char>::move(reinterpret_cast<char*>(dest),
                                   reinterpret_cast<const char*>(src), n));
  }
  static char_type* assign(char_type* s, size_t n, char_type a) {
    for (size_t i = 0; i < n; ++i) s[i] = a;
    return s;
  }
  static int_type to_int_type(char_type c) noexcept { return static_cast<int_type>(c); }
  static char_type to_char_type(int_type c) noexcept { return static_cast<char_type>(c); }
  static bool eq_int_type(int_type c1, int_type c2) noexcept { return c1 == c2; }
  static int_type eof() noexcept { return EOF; }
  static char_type not_eof(int_type c) noexcept {
    return c == EOF ? to_char_type(int_type(0)) : to_char_type(c);
  }
};
} // namespace std