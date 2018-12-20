#pragma once

#include <string>

namespace jsproperty {

class extractor {
 public:
  explicit extractor(const std::string &prop) : _prop(prop) {}

  void feed(char *data, size_t size) {
    if (!size || _extracted) {
      return;
    }
    if (_len < 0) {
      findField(data, size);
    } else if (_len < _prop.size()) {
      extractField(data, size);
    } else {
      extractValue(data, size);
    }
  }

 private:
  void findField(char *data, size_t size) {
    while (size && data[0] != '"') {
      ++data; --size;
    }
    if (size) {
      ++_len; ++data; --size;
    }
    feed(data, size);
  }

  void extractField(char *data, size_t size) {
    while (size) {
      if (data[0] == _prop[_len]) {
        ++_len; ++data; --size;
      } else {
        _len = -1;
        break;
      }
      if (_len == _prop.size()) {
        break;
      }
    }
    feed(data, size);
  }

  void extractValue(char *data, size_t size) {
    while (size && (data[0] == ':' || data[0] == '"' || std::isspace(data[0]))) {
      ++data; --size;
    }
    if (!size) {
      return;
    }
    while (data[0] != ',' && data[0] != '"' && data[0] != '}') {
      _value += data[0];
      ++data; --size;
      if (!size) {
        return;
      }
    }
    _extracted = true;
  }

 public:
  operator bool() const { return _extracted; }
  const std::string &operator*() const { return _value; }
  const std::string *operator->() const { return &_value; }

 private:
  const std::string _prop;
  std::string _value;
  int _len{-1};
  bool _extracted{false};
};

}  // namespace jsproperty
