#include "spoteefax_image.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

namespace spoteefax {
namespace image {
namespace {

using ColorMap = std::map<const Color *, size_t>;

const Color &getBestMatchingColor(unsigned char *rgb) {
  auto min_distance = 3 * 255 * 255;
  const Color *best_matching;
  for (auto &color : kColors) {
    const auto distance = color.distance(rgb[0], rgb[1], rgb[2]);
    if (distance <= min_distance) {
      min_distance = distance;
      best_matching = &color;
    }
  }
  return *best_matching;
}

ColorMap makeColorMap(const Color **begin, const Color **end) {
  ColorMap map;
  for (auto it = begin; it != end; ++it) {
    ++map[*it];
  }
  return map;
}

void mergeColorMap(ColorMap &map, const ColorMap &other) {
  for (auto &it : other) {
    map[it.first] += it.second;
  }
}

bool colorMapCompare(const ColorMap::value_type &lhs, const ColorMap::value_type &rhs) {
  return lhs.second < rhs.second;
}

const Color *topColor(const ColorMap &colors) {
  return std::max_element(colors.begin(), colors.end(), colorMapCompare)->first;
}

}  // namespace

Image::Image(size_t width, size_t height)
    : _width{width}, _height{height}, _pixels(width * height * 6), _lines(height) {}

const Color *&Image::pixel(size_t x, size_t y) {
  return _pixels[(y / 3) * _width * 6 + (x / 2) * 6 + 2 * (y % 3) + (x % 2)];
}

const Color *Image::pixel(size_t x, size_t y) const {
  return _pixels[(y / 3) * _width * 6 + (x / 2) * 6 + 2 * (y % 3) + (x % 2)];
}

unsigned char Image::get(size_t x, size_t y) const {
  return pixel(x, y)->terminal_code;
}

void Image::setSrc(size_t src_width, size_t src_height, int src_comp, unsigned char *src) {
  if (src_comp != 3) {
    throw std::invalid_argument{"components not rgb"};
  }
  const auto row_stride = src_width * src_comp;
  const auto pixel_width = 2 * _width;
  const auto pixel_height = 3 * _height;
  const auto scale = static_cast<float>(src_height) / pixel_height;

  for (auto y = 0u; y < pixel_height; ++y) {
    for (auto x = 0u; x < pixel_width; ++x) {
      // downsample
      auto src_x = std::min<size_t>(scale * x, src_width);
      auto src_y = std::min<size_t>(scale * y, src_height);
      auto *rgb = &src[src_y * row_stride + src_x * src_comp];
      // map pixel to valid color
      pixel(x, y) = &getBestMatchingColor(rgb);
    }
  }
  for (auto y = 0u; y < _height; ++y) {
    auto *row = &_pixels[y * _width * 6];
    auto colors = makeColorMap(row, row + _width * 6);
    if (colors.count(&kColors[0]) && colors[&kColors[0]] >= colors.size() * 6) {
      _lines[y] = renderRow(y, &kColors[0]);
    } else {
      _lines[y] = renderRow(y, topColor(colors));
    }
  }
}

void Image::clear() {
  for (auto &line : _lines) {
    line.clear();
  }
}

std::string Image::renderRow(size_t y, const Color *background) {
  auto *start = &_pixels[y * _width * 6];
  auto *end = &_pixels[(y + 1) * _width * 6];
  auto *pixel = start;

  std::string line;
  if (background == &kColors[0]) {
    start -= 6;
  } else {
    line += '\u001b';
    line += background->code;
    start += 6;
    line += '\u001b';
    line += ']';
    pixel += 6;
  }

  ColorMap colors;
  for (; pixel != end; pixel += 6) {
    auto pixel_colors = makeColorMap(pixel, pixel + 6);
    mergeColorMap(colors, pixel_colors);
    colors.erase(&kColors[0]);
    colors.erase(background);

    if (!colors.empty() && pixel_colors[background] == 6) {
      line += '\u001b';
      line += topColor(colors)->code;
      start += 6;
      while (start != pixel) {
        line += renderPixels(start, background);
        start += 6;
      }
      colors.clear();
    }
  }

  if (!colors.empty()) {
    line += '\u001b';
    line += topColor(colors)->code;
    start += 6;
  }
  while (start != end) {
    line += renderPixels(start, background);
    start += 6;
  }
  line += '\u001b';
  line += '\\';

  return line;
}

unsigned char Image::renderPixels(const Color **pixel, const Color *background) {
  unsigned char out = (1 << 5);
  for (auto bit = 0; bit < 6; ++bit) {
    if (pixel[bit] != background) {
      out |= 1 << (bit == 5 ? 6 : bit);
    }
  }
  return out;
}

}  // namespace image
}  // namespace spoteefax
