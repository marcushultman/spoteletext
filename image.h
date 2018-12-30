#pragma once

#include <array>
#include <string>
#include <vector>

namespace teletext {

struct Color {
  const char code;
  const unsigned char terminal_code;
  const unsigned char _r, _g, _b;
  Color(char code, unsigned char terminal_code, unsigned char r, unsigned char g, unsigned char b)
      : code(code), terminal_code(terminal_code), _r(r), _g(g), _b(b) {}

  int distance(int r, int g, int b) const {
    r -= _r;
    g -= _g;
    b -= _b;
    return r * r + g * g + b * b;
  }
};

const std::array<Color, 8> kColors = {
    Color{' ', 40, 0, 0, 0},        // black
    Color{'Q', 41, 255, 0, 0},      // red
    Color{'R', 42, 0, 255, 0},      // green
    Color{'S', 43, 255, 255, 0},    // yellow
    Color{'T', 44, 0, 0, 255},      // blue
    Color{'U', 45, 255, 0, 255},    // magenta
    Color{'V', 46, 0, 255, 255},    // cyan
    Color{'W', 47, 255, 255, 255},  // white
};

class Image {
 public:
  Image(size_t width, size_t height);
  size_t width() const { return _width; }
  size_t height() const { return _height; }
  std::string line(size_t i) const { return _lines[i]; }

  void setSrc(size_t src_width, size_t src_height, int src_comp, unsigned char *src);
  void clear();

  unsigned char get(size_t x, size_t y) const;

 private:
  const Color *&pixel(size_t x, size_t y);
  const Color *pixel(size_t x, size_t y) const;

  std::string renderRow(size_t y, const Color *background);
  unsigned char renderPixels(const Color **pixel, const Color *background);

  const size_t _width;
  const size_t _height;
  std::vector<const Color *> _pixels;
  std::vector<std::string> _lines;
};

}  // namespace teletext
