#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "Traits.h"
#include "Types.h"

namespace base::serializer {

struct JsonWriter
    : public BaseSerializer<Direction::Serialize, IsValueBinary::False> {
  nlohmann::json& out;
  const char* key_ = nullptr;

  JsonWriter(nlohmann::json& json) : out(json) {}

  template <typename T>
  void access(const char* key, const T& value) {
    nlohmann::json subj;
    if (key) {
			key_ = key;
		}
    JsonWriter sub(subj);
    if (key_) {
      out[key_] = subj;
    }
    else {
      out = subj;
		}
  }



  void write_bytes(const void* data, size_t size) {
    out = std::string(reinterpret_cast<const char*>(data), size);
  }
};

struct JsonReader
    : public BaseSerializer<Direction::Deserialize, IsValueBinary::False> {
  const nlohmann::json& out;

  JsonReader(const nlohmann::json& json) : out(json) {}

  template <typename T>
  void access(const char* key, T& value) {
    JsonReader sub(out.at(key));
    serialize_or_deserialize_impl<Direction::Deserialize>(sub, value);
  }

  void read_bytes(void* data, size_t size) {
    const std::string& s = out.get_ref<const std::string&>();
    std::memcpy(data, s.data(), std::min(size, s.size()));
  }
};

}  // namespace base::serializer