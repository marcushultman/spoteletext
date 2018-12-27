#pragma once

#include <array>
#include <string>
#include <vector>

namespace spoteefax {
namespace image {

struct Color {
  const char code;
  const char terminal_code;
  const unsigned char _r, _g, _b;
  Color(char code, int terminal_code, unsigned char r, unsigned char g, unsigned char b)
      : code(code), terminal_code(terminal_code), _r(r), _g(g), _b(b) {}

  int distance(int r, int g, int b) const {
    r -= _r;
    g -= _g;
    b -= _b;
    return r * r + g * g + b * b;
  }
};

const std::array<Color, 8> kColors = {
    Color{'\\', 40, 0, 0, 0},        // black
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
  Image(int width, int height);
  int width() const { return _width; }
  int height() const { return _height; }
  std::string line(size_t i) const { return _lines[i]; }

  void setSrc(int src_width, int src_height, int src_comp, unsigned char *src);

  unsigned char get(int x, int y) const;

 private:
  const Color *&pixel(int x, int y);
  const Color *pixel(int x, int y) const;

  std::string renderRow(int y, const Color *background);
  unsigned char renderPixels(const Color **pixel, const Color *background);

  const int _width;
  const int _height;
  std::vector<const Color *> _pixels;
  std::vector<std::string> _lines;
};

}  // namespace image
}  // namespace spoteefax
