#include <DashboardProtocol/DashboardProtocol.h>
#include <PersonalVersion/PersonalVersion.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

static void require(bool ok, const char* message) {
  if (!ok) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main(int argc, char** argv) {
  using personal_version::isNewer;
  require(isNewer("1.6.5-lxol.2", "1.6.5-lxol.1"), "personal revision must update");
  require(isNewer("1.7.0-lxol.1", "1.6.5-lxol.10"), "upstream base must update");
  require(isNewer("1.6.5-lxol.2", "1.6.5-lxol.1-dev-x3-personal-deadbeef"), "development build must update");
  for (const auto* bad : {"1.6.5", "1.7.0", "1.6.5-lxol.1", "1.6.4-lxol.99", "broken", "1.6.5-lxol.",
                          "1.6.5-lxol.999999999999999999999", "1.6.5-lxol.2oops"}) {
    require(!isNewer(bad, "1.6.5-lxol.1"), "refuse malformed, stock, same and older releases");
  }
  require(dashboard::validBaseUrl("http://dashboard.local:8090/x3"), "accept local directory");
  for (const auto* bad :
       {"", "http://", "https://host/x3", "http:///x3", "http://:80/x3", "http://user:secret@host/x3",
        "http://host/x3?token=1", "http://host/#fragment", "http://host/with space", "http://host/\n"}) {
    require(!dashboard::validBaseUrl(bad), "reject unsupported URLs");
  }
  require(!dashboard::validBaseUrl(std::string(192, 'x')), "bound URL memory");

  // Header captured from Pillow's actual 528x792 monochrome output.
  std::vector<uint8_t> bytes = {0x42, 0x4d, 0x9e, 0xd2, 0, 0, 0,    0,    0, 0, 0x3e, 0,    0,    0,   0x28, 0,
                                0,    0,    0x10, 0x02, 0, 0, 0x18, 0x03, 0, 0, 1,    0,    1,    0,   0,    0,
                                0,    0,    0x60, 0xd2, 0, 0, 0xc4, 0x0e, 0, 0, 0xc4, 0x0e, 0,    0,   2,    0,
                                0,    0,    2,    0,    0, 0, 0,    0,    0, 0, 0xff, 0xff, 0xff, 0xff};
  size_t size = 53918;
  if (argc > 1) {
    std::ifstream file(argv[1], std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(file), {});
    size = bytes.size();
  }
  {
    require(bytes.size() >= dashboard::BMP_HEADER_SIZE, "fixture missing");
    require(dashboard::validBmp(bytes.data(), size, 528, 792), "accept server BMP");
    require(!dashboard::validBmp(bytes.data(), size - 1, 528, 792), "reject truncated body");
    require(!dashboard::validBmp(bytes.data(), size, 480, 800), "reject another panel's image");
    for (unsigned offset : {0u, 2u, 10u, 14u, 18u, 22u, 26u, 28u, 30u, 54u, 58u}) {
      bytes[offset] ^= 1;
      require(!dashboard::validBmp(bytes.data(), size, 528, 792), "reject malformed BMP header");
      bytes[offset] ^= 1;
    }
  }
  std::cout << "Personal version, URL and image validation passed\n";
}
