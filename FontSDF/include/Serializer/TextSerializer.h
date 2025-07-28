#pragma once
#include <cstdint>
#include <string>

#include "Traits.h"
#include "Types.h"

namespace base::serializer {

struct TextWriter
    : public BaseSerializer<Direction::Serialize, IsValueBinary::False> {
  std::ostream& out;
  size_t pos;

  TextWriter(std::ostream& os) : out(os), pos(0) {}

  template <typename T>
    requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
  void access(const char* key, const T& value) {
    if (key) {
      out << key << " ";
    } else {
      out << "value ";
    }
    out << value;
    out << "\n";
    pos += sizeof(T);
  }

  void access(const char* key, const std::string& value) {
    if (key) {
      out << key << " ";
    } else {
      out << "value ";
    }

    out << value;
    out << "\n";
    pos += value.size() + 1;

  }
};

struct TextReader
    : public BaseSerializer<Direction::Deserialize, IsValueBinary::False> {
  std::istream& in;
  size_t pos;
  TextReader(std::istream& is) : in(is), pos(0) {}
  template <typename T>
		requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
  void access(const char* key, T& value) {
    in >> dummy_key_;
    in >> value;
    pos += sizeof(T);
  }
  void access(const char* key, std::string& value) {
    in >> dummy_key_;
    in >> value;
    pos += value.size() + 1;
  }

 private:
  std::string dummy_key_;
};

}  // namespace base::serializer