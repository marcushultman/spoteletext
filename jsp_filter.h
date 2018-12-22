#pragma once

#include <string>
#include "jspextractor.h"

namespace jsproperty {
namespace {

size_t findStart(const std::string &buffer, const char* prop, size_t indent) {

  size_t i = 0, prev = 0, current_indent = 0;
  while (i != std::string::npos) {
    i = buffer.find(prop, (prev = i) + 1);
    if (i == std::string::npos) {
      break;
    }
    for (; prev < i; ++prev) {
      current_indent += buffer[prev] == '{' ? 1 : buffer[prev] == '}' ? -1 : 0;
    }
    if (current_indent == indent) {
      break;
    }
  }
  return i;
}

} // namespace

void filter(
    const std::string &buffer, const char* prop, size_t indent, size_t skip, extractor &extractor) {
  auto i = findStart(buffer, prop, indent);
  if (i == std::string::npos) {
    return;
  }
  indent = 1;
  for (i = buffer.find("{", i) + 1; !extractor && i < buffer.size(); ++i) {
    indent += buffer[i] == '{' ? 1 : buffer[i] == '}' ? -1 : 0;
    if (indent == 1) {
      extractor.feed(&buffer[i], 1);
      if (extractor && skip) {
        extractor.reset();
        --skip;
      }
    }
  }
}

}  //namespace jsproperty
