#pragma once

#include <iostream>
#pragma once
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mj {


struct Error {
  size_t offset = 0;
  const char* message = "";
  explicit operator bool() const noexcept { return message && *message; }
};

struct Str {
  std::string_view view{};
  std::string
      owned{};
  bool owning() const noexcept { return !owned.empty(); }
  std::string_view sv() const noexcept {
    return owning() ? std::string_view(owned) : view;
  }
};

struct Value;

using Array = std::vector<Value>;
using Object = std::vector<std::pair<Str, Value>>;

using Number = std::variant<int64_t, double>;
struct Value : std::variant<std::monostate, bool, Number, Str, Array, Object> {
  using variant::variant;
  bool is_null() const noexcept {
    return std::holds_alternative<std::monostate>(*this);
  }
  bool is_bool() const noexcept { return std::holds_alternative<bool>(*this); }
  bool is_num() const noexcept { return std::holds_alternative<Number>(*this); }
  bool is_str() const noexcept { return std::holds_alternative<Str>(*this); }
  bool is_array() const noexcept {
    return std::holds_alternative<Array>(*this);
  }
  bool is_object() const noexcept {
    return std::holds_alternative<Object>(*this);
  }

  const Number* number() const noexcept { return std::get_if<Number>(this); }
  const Str* string() const noexcept { return std::get_if<Str>(this); }
  const Array* array() const noexcept { return std::get_if<Array>(this); }
  const Object* object() const noexcept { return std::get_if<Object>(this); }
};

class Parser {
 public:
  Parser(std::string_view json, Error* e)
      : begin_(json.data()),
        p_(json.data()),
        end_(json.data() + json.size()),
        err_(e) {}

  bool parse(Value& out) {
    skip_ws();
    if (!parse_value(out)) return false;
    skip_ws();
    if (p_ != end_) return fail("Trailing characters after JSON");
    return true;
  }

 private:
  const char* begin_;
  const char* p_;
  const char* end_;
  Error* err_;

  [[nodiscard]] bool eof() const noexcept { return p_ >= end_; }
  [[nodiscard]] size_t off() const noexcept {
    return static_cast<size_t>(p_ - begin_);
  }

  void skip_ws() noexcept {
    while (!eof()) {
      char c = *p_;
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
        ++p_;
        continue;
      }
      break;
    }
  }

  bool fail(const char* msg) {
    if (err_) {
      err_->offset = off();
      err_->message = msg;
    }
    return false;
  }

  bool parse_value(Value& out) {
    if (eof()) return fail("Unexpected end of input");
    switch (*p_) {
      case 'n':
        return parse_lit("null", out, std::monostate{});
      case 't':
        return parse_lit("true", out, true);
      case 'f':
        return parse_lit("false", out, false);
      case '"': {
        Str s;
        if (!parse_string(s)) return false;
        out = std::move(s);
        return true;
      }
      case '[': {
        Array a;
        if (!parse_array(a)) return false;
        out = std::move(a);
        return true;
      }
      case '{': {
        Object o;
        if (!parse_object(o)) return false;
        out = std::move(o);
        return true;
      }
      default:
        return parse_number(out);
    }
  }

  template <class T>
  bool parse_lit(const char* lit, Value& out, T v) {
    const char* q = p_;
    for (; *lit; ++lit, ++q) {
      if (q >= end_ || *q != *lit) return fail("Invalid literal");
    }
    p_ = q;
    out = std::move(v);
    return true;
  }

  static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  static void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F)
      out.push_back(char(cp));
    else if (cp <= 0x7FF) {
      out.push_back(char(0xC0 | (cp >> 6)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out.push_back(char(0xE0 | (cp >> 12)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (cp >> 18)));
      out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    }
  }

  bool parse_string(Str& out) {
    if (*p_ != '"') return fail("Expected '\"' for string");
    const char* s = ++p_;
    const char* start = s;

    while (p_ < end_) {
      unsigned char c = static_cast<unsigned char>(*p_);
      if (c == '"') {
        out.view = std::string_view(start, p_ - start);
        ++p_;
        return true;
      }
      if (c == '\\' || c < 0x20) break;
      ++p_;
    }
    if (p_ >= end_) return fail("Unterminated string");

    std::string buf;
    buf.reserve(static_cast<size_t>((p_ - start) + 16));
    buf.append(start, p_ - start);

    while (p_ < end_) {
      unsigned char c = static_cast<unsigned char>(*p_++);
      if (c == '"') {
        out.owned = std::move(buf);
        return true;
      }
      if (c == '\\') {
        if (eof()) return fail("Bad escape");
        char e = *p_++;
        switch (e) {
          case '"':
            buf.push_back('"');
            break;
          case '\\':
            buf.push_back('\\');
            break;
          case '/':
            buf.push_back('/');
            break;
          case 'b':
            buf.push_back('\b');
            break;
          case 'f':
            buf.push_back('\f');
            break;
          case 'n':
            buf.push_back('\n');
            break;
          case 'r':
            buf.push_back('\r');
            break;
          case 't':
            buf.push_back('\t');
            break;
          case 'u': {
            if (end_ - p_ < 4) return fail("Bad \\u escape");
            int h0 = hexv(p_[0]), h1 = hexv(p_[1]), h2 = hexv(p_[2]),
                h3 = hexv(p_[3]);
            if ((h0 | h1 | h2 | h3) < 0) return fail("Bad \\u hex");
            uint32_t cp = (h0 << 12) | (h1 << 8) | (h2 << 4) | h3;
            p_ += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (end_ - p_ < 6 || p_[0] != '\\' || p_[1] != 'u')
                return fail("Isolated high surrogate");
              int g0 = hexv(p_[2]), g1 = hexv(p_[3]), g2 = hexv(p_[4]),
                  g3 = hexv(p_[5]);
              if ((g0 | g1 | g2 | g3) < 0) return fail("Bad low surrogate");
              uint32_t low = (g0 << 12) | (g1 << 8) | (g2 << 4) | g3;
              if (low < 0xDC00 || low > 0xDFFF)
                return fail("Invalid low surrogate");
              p_ += 6;
              cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              return fail("Isolated low surrogate");
            }
            append_utf8(buf, cp);
          } break;
          default:
            return fail("Unknown escape");
        }
      } else if (c < 0x20) {
        return fail("Control char in string");
      } else {
        buf.push_back(char(c));
      }
    }
    return fail("Unterminated string");
  }

  bool parse_number(Value& out) {
    const char* s = p_;
    if (*p_ == '-') ++p_;
    if (eof()) return fail("Invalid number");
    if (*p_ == '0') {
      ++p_;
    } else {
      if (!std::isdigit(static_cast<unsigned char>(*p_)))
        return fail("Invalid number");
      while (p_ < end_ && std::isdigit(static_cast<unsigned char>(*p_))) ++p_;
    }
    bool is_float = false;
    if (p_ < end_ && *p_ == '.') {
      is_float = true;
      ++p_;
      if (p_ >= end_ || !std::isdigit(static_cast<unsigned char>(*p_)))
        return fail("Invalid fraction");
      while (p_ < end_ && std::isdigit(static_cast<unsigned char>(*p_))) ++p_;
    }
    if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
      is_float = true;
      ++p_;
      if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
      if (p_ >= end_ || !std::isdigit(static_cast<unsigned char>(*p_)))
        return fail("Invalid exponent");
      while (p_ < end_ && std::isdigit(static_cast<unsigned char>(*p_))) ++p_;
    }
    std::string_view numsv(s, size_t(p_ - s));
    if (!is_float) {
      int64_t iv{};
      auto res =
          std::from_chars(numsv.data(), numsv.data() + numsv.size(), iv, 10);
      if (res.ec == std::errc{}) {
        out = Number{iv};
        return true;
      }
    }
    double dv{};
    auto res = std::from_chars(numsv.data(), numsv.data() + numsv.size(), dv);
    if (res.ec != std::errc{}) return fail("Invalid number");
    out = Number{dv};
    return true;
  }

  bool parse_array(Array& out) {
    if (*p_ != '[') return fail("Expected '['");
    ++p_;
    skip_ws();
    if (!eof() && *p_ == ']') {
      ++p_;
      return true;
    }
    for (;;) {
      skip_ws();
      Value v;
      if (!parse_value(v)) return false;
      out.emplace_back(std::move(v));
      skip_ws();
      if (eof()) return fail("Unterminated array");
      char c = *p_++;
      if (c == ']') break;
      if (c != ',') return fail("Expected ',' or ']'");
    }
    return true;
  }

  bool parse_object(Object& out) {
    if (*p_ != '{') return fail("Expected '{'");
    ++p_;
    skip_ws();
    if (!eof() && *p_ == '}') {
      ++p_;
      return true;
    }
    for (;;) {
      skip_ws();
      if (eof() || *p_ != '"') return fail("Object key must be string");
      Str key;
      if (!parse_string(key)) return false;
      skip_ws();
      if (eof() || *p_ != ':') return fail("Expected ':' after key");
      ++p_;
      skip_ws();
      Value val;
      if (!parse_value(val)) return false;
      out.emplace_back(std::move(key), std::move(val));
      skip_ws();
      if (eof()) return fail("Unterminated object");
      char c = *p_++;
      if (c == '}') break;
      if (c != ',') return fail("Expected ',' or '}'");
    }
    return true;
  }
};

inline bool parse(std::string_view json, Value& out, Error& err) {
  err = {};
  Parser p(json, &err);
  return p.parse(out);
}

inline const Value* find(const Object& obj, std::string_view key) {
  for (const auto& [k, v] : obj)
    if (k.sv() == key) return &v;
  return nullptr;
}

}

int jsonHoge() {
  const std::string json = R"({
        "title": "Mini JSON",
        "version": 1,
        "pi": 3.14159,
        "ok": true,
        "tags": ["fast","tiny","C++20"],
        "meta": { "author": "you", "year": 2025 }
    })";

  mj::Value root;
  mj::Error err;
  if (!mj::parse(json, root, err)) {
    std::cerr << "Parse error at " << err.offset << ": " << err.message << "\n";
    return 1;
  }

  // ルートはオブジェクト想定
  const auto* obj = std::get_if<mj::Object>(&root);
  if (!obj) {
    std::cerr << "Root is not an object\n";
    return 1;
  }

  // 文字列
  if (const mj::Value* v = mj::find(*obj, "title")) {
    if (const auto* s = std::get_if<mj::Str>(v)) {
      std::cout << "title: " << s->sv() << "\n";
    }
  }

  // 数値（int64_t or double のどちらか）
  if (const mj::Value* v = mj::find(*obj, "version")) {
    if (const auto* num = v->number()) {
      if (const auto* iv = std::get_if<int64_t>(num)) {
        std::cout << "version: " << *iv << "\n";
      } else {
        std::cout << "version: " << std::get<double>(*num) << "\n";
      }
    }
  }
  if (const mj::Value* v = mj::find(*obj, "pi")) {
    if (const auto* num = v->number()) {
      if (const auto* iv = std::get_if<int64_t>(num)) {
        std::cout << "pi (int): " << *iv << "\n";
      } else {
        std::cout << "pi (double): " << std::get<double>(*num) << "\n";
      }
    }
  }

  // 配列
  if (const mj::Value* v = mj::find(*obj, "tags")) {
    if (const auto* arr = std::get_if<mj::Array>(v)) {
      std::cout << "tags:";
      for (const auto& e : *arr) {
        if (const auto* s = std::get_if<mj::Str>(&e)) {
          std::cout << " " << s->sv();
        }
      }
      std::cout << "\n";
    }
  }

  // ネストしたオブジェクトのキー参照
  if (const mj::Value* v = mj::find(*obj, "meta")) {
    if (const auto* meta = std::get_if<mj::Object>(v)) {
      if (const mj::Value* a = mj::find(*meta, "author")) {
        if (const auto* s = std::get_if<mj::Str>(a)) {
          std::cout << "author: " << s->sv() << "\n";
        }
      }
      if (const mj::Value* y = mj::find(*meta, "year")) {
        if (const auto* num = y->number()) {
          if (const auto* iv = std::get_if<int64_t>(num))
            std::cout << "year: " << *iv << "\n";
          else
            std::cout << "year: " << std::get<double>(*num) << "\n";
        }
      }
    }
  }

  // ok フラグ
  if (const mj::Value* v = mj::find(*obj, "ok")) {
    if (const auto* b = std::get_if<bool>(v)) {
      std::cout << "ok: " << (*b ? "true" : "false") << "\n";
    }
  }

  return 0;
}