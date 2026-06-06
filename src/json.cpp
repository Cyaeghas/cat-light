#include "json.hpp"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace catlight {
namespace {

const std::string empty_string;
const Json::array_type empty_array;
const Json::object_type empty_object;

class Parser {
public:
  explicit Parser(std::string_view input) : input_(input) {}

  Json parse() {
    skip_ws();
    Json value = parse_value();
    skip_ws();
    if (pos_ != input_.size()) {
      fail("unexpected trailing characters");
    }
    return value;
  }

private:
  Json parse_value() {
    skip_ws();
    if (pos_ >= input_.size()) {
      fail("unexpected end of input");
    }
    char c = input_[pos_];
    if (c == 'n') {
      expect("null");
      return Json();
    }
    if (c == 't') {
      expect("true");
      return Json(true);
    }
    if (c == 'f') {
      expect("false");
      return Json(false);
    }
    if (c == '"') {
      return Json(parse_string());
    }
    if (c == '[') {
      return parse_array();
    }
    if (c == '{') {
      return parse_object();
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      return Json(parse_number());
    }
    fail("unexpected character");
  }

  Json parse_array() {
    ++pos_;
    Json::array_type values;
    skip_ws();
    if (peek(']')) {
      ++pos_;
      return Json(std::move(values));
    }
    while (true) {
      values.push_back(parse_value());
      skip_ws();
      if (peek(']')) {
        ++pos_;
        break;
      }
      require(',');
    }
    return Json(std::move(values));
  }

  Json parse_object() {
    ++pos_;
    Json::object_type values;
    skip_ws();
    if (peek('}')) {
      ++pos_;
      return Json(std::move(values));
    }
    while (true) {
      skip_ws();
      if (!peek('"')) {
        fail("expected object key");
      }
      std::string key = parse_string();
      skip_ws();
      require(':');
      values.emplace(std::move(key), parse_value());
      skip_ws();
      if (peek('}')) {
        ++pos_;
        break;
      }
      require(',');
    }
    return Json(std::move(values));
  }

  std::string parse_string() {
    require('"');
    std::string out;
    while (pos_ < input_.size()) {
      char c = input_[pos_++];
      if (c == '"') {
        return out;
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= input_.size()) {
        fail("unterminated escape");
      }
      char esc = input_[pos_++];
      switch (esc) {
      case '"':
      case '\\':
      case '/':
        out.push_back(esc);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u':
        append_unicode_escape(out);
        break;
      default:
        fail("invalid escape");
      }
    }
    fail("unterminated string");
  }

  void append_unicode_escape(std::string &out) {
    if (pos_ + 4 > input_.size()) {
      fail("short unicode escape");
    }
    unsigned code = 0;
    for (int i = 0; i < 4; ++i) {
      char c = input_[pos_++];
      code <<= 4;
      if (c >= '0' && c <= '9') {
        code += static_cast<unsigned>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        code += static_cast<unsigned>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        code += static_cast<unsigned>(c - 'A' + 10);
      } else {
        fail("invalid unicode escape");
      }
    }
    if (code <= 0x7F) {
      out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  double parse_number() {
    size_t start = pos_;
    if (peek('-')) {
      ++pos_;
    }
    while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
      ++pos_;
    }
    if (peek('.')) {
      ++pos_;
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
      }
    }
    if (peek('e') || peek('E')) {
      ++pos_;
      if (peek('+') || peek('-')) {
        ++pos_;
      }
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
      }
    }
    return std::stod(std::string(input_.substr(start, pos_ - start)));
  }

  void expect(std::string_view literal) {
    if (input_.substr(pos_, literal.size()) != literal) {
      fail("expected literal");
    }
    pos_ += literal.size();
  }

  void require(char c) {
    skip_ws();
    if (!peek(c)) {
      fail(std::string("expected '") + c + "'");
    }
    ++pos_;
  }

  bool peek(char c) const {
    return pos_ < input_.size() && input_[pos_] == c;
  }

  void skip_ws() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
      ++pos_;
    }
  }

  [[noreturn]] void fail(const std::string &message) const {
    std::ostringstream oss;
    oss << message << " at byte " << pos_;
    throw std::runtime_error(oss.str());
  }

  std::string_view input_;
  size_t pos_ = 0;
};

std::string escape_string(const std::string &value) {
  std::ostringstream out;
  out << '"';
  for (char c : value) {
    switch (c) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(static_cast<unsigned char>(c));
      } else {
        out << c;
      }
    }
  }
  out << '"';
  return out.str();
}

void dump_impl(const Json &value, std::ostringstream &out, int indent, int depth) {
  const bool pretty = indent >= 0;
  switch (value.type()) {
  case Json::Type::Null:
    out << "null";
    break;
  case Json::Type::Bool:
    out << (value.as_bool() ? "true" : "false");
    break;
  case Json::Type::Number: {
    double n = value.as_number();
    if (std::isfinite(n) && std::fabs(n - std::round(n)) < 0.0000001) {
      out << static_cast<long long>(std::llround(n));
    } else {
      out << std::setprecision(15) << n;
    }
    break;
  }
  case Json::Type::String:
    out << escape_string(value.as_string());
    break;
  case Json::Type::Array: {
    out << '[';
    const auto &arr = value.array();
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i) {
        out << ',';
      }
      if (pretty) {
        out << '\n' << std::string((depth + 1) * indent, ' ');
      }
      dump_impl(arr[i], out, indent, depth + 1);
    }
    if (pretty && !arr.empty()) {
      out << '\n' << std::string(depth * indent, ' ');
    }
    out << ']';
    break;
  }
  case Json::Type::Object: {
    out << '{';
    const auto &obj = value.object();
    size_t i = 0;
    for (const auto &entry : obj) {
      if (i++) {
        out << ',';
      }
      if (pretty) {
        out << '\n' << std::string((depth + 1) * indent, ' ');
      }
      out << escape_string(entry.first) << (pretty ? ": " : ":");
      dump_impl(entry.second, out, indent, depth + 1);
    }
    if (pretty && !obj.empty()) {
      out << '\n' << std::string(depth * indent, ' ');
    }
    out << '}';
    break;
  }
  }
}

} // namespace

Json::Json() = default;
Json::Json(std::nullptr_t) {}
Json::Json(bool value) : type_(Type::Bool), bool_value_(value) {}
Json::Json(double value) : type_(Type::Number), number_value_(value) {}
Json::Json(int value) : type_(Type::Number), number_value_(static_cast<double>(value)) {}
Json::Json(std::string value) : type_(Type::String), string_value_(std::move(value)) {}
Json::Json(const char *value) : Json(std::string(value ? value : "")) {}
Json::Json(array_type value) : type_(Type::Array), array_value_(std::move(value)) {}
Json::Json(object_type value) : type_(Type::Object), object_value_(std::move(value)) {}

Json Json::parse(std::string_view text, std::string *error) {
  try {
    return Parser(text).parse();
  } catch (const std::exception &e) {
    if (error) {
      *error = e.what();
    }
    return Json();
  }
}

bool Json::as_bool(bool fallback) const {
  return is_bool() ? bool_value_ : fallback;
}

double Json::as_number(double fallback) const {
  return is_number() ? number_value_ : fallback;
}

int Json::as_int(int fallback) const {
  return is_number() ? static_cast<int>(std::round(number_value_)) : fallback;
}

const std::string &Json::as_string() const {
  return is_string() ? string_value_ : empty_string;
}

std::string Json::string_or(std::string fallback) const {
  return is_string() ? string_value_ : std::move(fallback);
}

Json::array_type &Json::array() {
  if (!is_array()) {
    type_ = Type::Array;
    array_value_.clear();
  }
  return array_value_;
}

const Json::array_type &Json::array() const {
  return is_array() ? array_value_ : empty_array;
}

Json::object_type &Json::object() {
  if (!is_object()) {
    type_ = Type::Object;
    object_value_.clear();
  }
  return object_value_;
}

const Json::object_type &Json::object() const {
  return is_object() ? object_value_ : empty_object;
}

Json &Json::operator[](const std::string &key) {
  return object()[key];
}

const Json *Json::get(const std::string &key) const {
  if (!is_object()) {
    return nullptr;
  }
  auto it = object_value_.find(key);
  return it == object_value_.end() ? nullptr : &it->second;
}

Json *Json::get(const std::string &key) {
  if (!is_object()) {
    return nullptr;
  }
  auto it = object_value_.find(key);
  return it == object_value_.end() ? nullptr : &it->second;
}

const Json *Json::path(std::initializer_list<std::string_view> keys) const {
  const Json *current = this;
  for (auto key : keys) {
    if (!current) {
      return nullptr;
    }
    current = current->get(std::string(key));
  }
  return current;
}

Json *Json::path(std::initializer_list<std::string_view> keys) {
  Json *current = this;
  for (auto key : keys) {
    if (!current) {
      return nullptr;
    }
    current = current->get(std::string(key));
  }
  return current;
}

std::string Json::dump(int indent) const {
  std::ostringstream out;
  dump_impl(*this, out, indent, 0);
  return out.str();
}

} // namespace catlight
