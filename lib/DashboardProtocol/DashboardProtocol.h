#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace dashboard {
constexpr size_t URL_CAPACITY = 192;
constexpr unsigned PAGE_COUNT = 3;
constexpr size_t BMP_HEADER_SIZE = 62;

// A local static image directory: no credentials, queries, redirects, or TLS.
inline bool validBaseUrl(std::string_view url) {
  if (url.size() < 8 || url.size() >= URL_CAPACITY || !url.starts_with("http://")) return false;
  const auto hostEnd = url.find('/', 7);
  const auto authority = url.substr(7, hostEnd == std::string_view::npos ? url.size() - 7 : hostEnd - 7);
  if (authority.empty() || authority.front() == ':' || authority.back() == ':') return false;
  for (const unsigned char ch : url) {
    if (ch <= 32 || ch >= 127 || ch == '?' || ch == '#' || ch == '@' || ch == '\\') return false;
  }
  return true;
}

inline uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

inline size_t bmpSize(unsigned width, unsigned height) { return BMP_HEADER_SIZE + ((width + 31) / 32 * 4) * height; }

// The server emits the narrow Windows BMP format we accept. Validate before
// promoting a download; the generic image decoder never sees network input.
inline bool validBmp(const uint8_t* h, size_t size, unsigned width, unsigned height) {
  return width > 0 && width <= 800 && height > 0 && height <= 800 && size == bmpSize(width, height) && h[0] == 'B' &&
         h[1] == 'M' && le32(h + 2) == size && le32(h + 10) == BMP_HEADER_SIZE && le32(h + 14) == 40 &&
         le32(h + 18) == width && le32(h + 22) == height && h[26] == 1 && h[27] == 0 && h[28] == 1 && h[29] == 0 &&
         le32(h + 30) == 0 && h[54] == 0 && h[55] == 0 && h[56] == 0 && h[58] == 255 && h[59] == 255 && h[60] == 255;
}
}  // namespace dashboard
