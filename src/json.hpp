#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace catlight {

class Json {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  using array_type = std::vector<Json>;
  using object_type = std::map<std::string, Json>;

  Json();
  Json(std::nullptr_t);
  Json(bool value);
  Json(double value);
  Json(int value);
  Json(std::string value);
  Json(const char *value);
  Json(array_type value);
  Json(object_type value);

  static Json parse(std::string_view text, std::string *error = nullptr);

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool(bool fallback = false) const;
  double as_number(double fallback = 0.0) const;
  int as_int(int fallback = 0) const;
  const std::string &as_string() const;
  std::string string_or(std::string fallback = "") const;

  array_type &array();
  const array_type &array() const;
  object_type &object();
  const object_type &object() const;

  Json &operator[](const std::string &key);
  const Json *get(const std::string &key) const;
  Json *get(const std::string &key);
  const Json *path(std::initializer_list<std::string_view> keys) const;
  Json *path(std::initializer_list<std::string_view> keys);

  std::string dump(int indent = -1) const;

private:
  Type type_ = Type::Null;
  bool bool_value_ = false;
  double number_value_ = 0.0;
  std::string string_value_;
  array_type array_value_;
  object_type object_value_;
};

} // namespace catlight
