#pragma once

#include <array>
#include <string_view>

namespace personal_version {
using Version = std::array<unsigned, 4>;

inline bool parse(std::string_view text, Version& parts, bool allowDevelopment = false) {
  if (text.starts_with('v')) text.remove_prefix(1);
  for (unsigned i = 0; i < parts.size(); ++i) {
    if (text.empty() || text.front() < '0' || text.front() > '9') return false;
    unsigned value = 0;
    while (!text.empty() && text.front() >= '0' && text.front() <= '9') {
      value = value * 10 + (text.front() - '0');
      if (value > 65535) return false;
      text.remove_prefix(1);
    }
    parts[i] = value;
    const std::string_view separator = i < 2 ? "." : (i == 2 ? "-lxol." : "");
    if (!text.starts_with(separator)) return false;
    text.remove_prefix(separator.size());
  }
  return text.empty() || (allowDevelopment && text.starts_with("-dev-"));
}

inline bool isNewer(std::string_view latest, std::string_view current) {
  Version a{}, b{};
  return parse(latest, a) && parse(current, b, true) && a > b;
}
}  // namespace personal_version
