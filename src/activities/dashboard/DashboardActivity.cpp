#include "DashboardActivity.h"

#include <HTTPClient.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>

#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char CONFIG_PATH[] = "/dashboard-url.txt";
constexpr char CACHE_DIR[] = "/.crosspoint/dashboard";
constexpr uint32_t TRANSFER_TIMEOUT_MS = 12000;
constexpr uint32_t SOCKET_TIMEOUT_MS = 3000;
constexpr uint32_t CONFIG_HOLD_MS = 1000;
}  // namespace

// Allocated once per refresh and released when all pages finish. HTTPClient,
// NetworkClient and the reusable 256-byte chunk do not fit on the task stack.
struct DashboardActivity::Transfer {
  HalPowerManager::Lock powerLock;
  WiFiClient client;
  HTTPClient http;
  HalFile file;
  uint8_t chunk[256];
  size_t received = 0;
  size_t expected = 0;
  uint32_t started = 0;
};

DashboardActivity::DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Dashboard", renderer, mappedInput), bitmap(imageFile) {}

DashboardActivity::~DashboardActivity() = default;

void DashboardActivity::cachePath(char* out, size_t size, unsigned number, const char* suffix) const {
  snprintf(out, size, "%s/page-%u%s", CACHE_DIR, number, suffix);
}

bool DashboardActivity::fileReady(const char* path) {
  HalFile file;
  uint8_t header[dashboard::BMP_HEADER_SIZE];
  return Storage.openFileForRead("DASH", path, file) && file.read(header, sizeof(header)) == sizeof(header) &&
         dashboard::validBmp(header, file.size(), renderer.getScreenWidth(), renderer.getScreenHeight());
}

void DashboardActivity::onEnter() {
  Activity::onEnter();
  RenderLock lock;
  savedOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  HalFile config;
  if (Storage.openFileForRead("DASH", CONFIG_PATH, config) && config.size() < sizeof(baseUrl)) {
    const int count = config.read(baseUrl, sizeof(baseUrl) - 1);
    if (count >= 0) baseUrl[count] = '\0';
  }
  baseUrl[strcspn(baseUrl, "\r\n")] = '\0';
  if (!dashboard::validBaseUrl(baseUrl)) {
    baseUrl[0] = '\0';
    state = State::NeedConfig;
  }
  requestUpdate();
}

void DashboardActivity::onExit() {
  imageFile.close();
  transfer.reset();
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
  // Follow upstream WiFi activities: reclaim fragmented network heap before
  // returning to reading. Cached pages themselves do not start WiFi.
  if (usedWifi) {
    WiFi.disconnect(false);
    silentRestart();
  }
}

void DashboardActivity::configure() {
  state = State::Showing;
  // The existing keyboard is heap-owned by ActivityManager; no persistent
  // keyboard buffer is added to the reader or dashboard.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_DASHBOARD_URL), baseUrl,
                                                           sizeof(baseUrl) - 1, InputType::Url);
  if (!keyboard) {
    LOG_ERR("DASH", "OOM: keyboard");
    notice = StrId::STR_DASHBOARD_FAILED;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    RenderLock lock;
    if (result.isCancelled) return;
    const auto* entry = std::get_if<KeyboardResult>(&result.data);
    if (!entry || !dashboard::validBaseUrl(entry->text)) {
      notice = StrId::STR_DASHBOARD_BAD_URL;
      return;
    }
    if (!Storage.writeFile(CONFIG_PATH, entry->text.c_str())) {
      notice = StrId::STR_DASHBOARD_FAILED;
      return;
    }
    snprintf(baseUrl, sizeof(baseUrl), "%s", entry->text.c_str());
    size_t len = strlen(baseUrl);
    while (len > 7 && baseUrl[len - 1] == '/') baseUrl[--len] = '\0';
    state = State::NeedWifi;
  });
}

void DashboardActivity::connect() {
  usedWifi = true;
  state = State::Showing;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("DASH", "OOM: WiFi selection");
    notice = StrId::STR_DASHBOARD_FAILED;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    RenderLock lock;
    const auto* wifiResult = std::get_if<WifiResult>(&result.data);
    if (result.isCancelled || !wifiResult || !wifiResult->connected) {
      stopTransfer(StrId::STR_DASHBOARD_CANCELLED);
      return;
    }
    downloadPage = 1;
    state = State::StartDownload;
    notice = StrId::STR_DASHBOARD_REFRESHING;
  });
}

void DashboardActivity::startDownload() {
  if (!transfer) transfer = makeUniqueNoThrow<Transfer>();
  if (!transfer) {
    LOG_ERR("DASH", "OOM: transfer (%u bytes)", static_cast<unsigned>(sizeof(Transfer)));
    stopTransfer(StrId::STR_DASHBOARD_FAILED);
    return;
  }
  if (!Storage.ensureDirectoryExists(CACHE_DIR)) {
    stopTransfer(StrId::STR_DASHBOARD_FAILED);
    return;
  }
  // URL is bounded by the SD config/keyboard; the temporary lives only while
  // opening a request, never in the render or body-copy loops.
  char url[dashboard::URL_CAPACITY + 20];
  snprintf(url, sizeof(url), "%s%spage-%u.bmp", baseUrl, baseUrl[strlen(baseUrl) - 1] == '/' ? "" : "/", downloadPage);
  auto& t = *transfer;
  t.http.setConnectTimeout(SOCKET_TIMEOUT_MS);
  t.http.setTimeout(SOCKET_TIMEOUT_MS);
  t.http.setReuse(false);
  t.http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  t.started = millis();
  t.received = 0;
  t.expected = dashboard::bmpSize(renderer.getScreenWidth(), renderer.getScreenHeight());
  if (!t.http.begin(t.client, url) || t.http.GET() != HTTP_CODE_OK || t.http.getSize() != t.expected) {
    LOG_ERR("DASH", "Invalid image response for page %u", downloadPage);
    stopTransfer(StrId::STR_DASHBOARD_FAILED);
    return;
  }
  // Reuse the URL buffer after the request starts; avoid another stack buffer.
  cachePath(url, sizeof(url), downloadPage, ".tmp");
  if (!Storage.openFileForWrite("DASH", url, t.file)) {
    stopTransfer(StrId::STR_DASHBOARD_FAILED);
    return;
  }
  state = State::Downloading;
}

bool DashboardActivity::promoteDownload() {
  char path[64], temp[64], backup[64];
  cachePath(path, sizeof(path), downloadPage);
  cachePath(temp, sizeof(temp), downloadPage, ".tmp");
  cachePath(backup, sizeof(backup), downloadPage, ".old");
  if (!fileReady(temp)) return false;
  // FAT cannot rename onto an existing file. Keep the last valid image as a
  // fallback across both rename operations and across power loss.
  if (fileReady(path)) {
    if (Storage.exists(backup) && !Storage.remove(backup)) return false;
    if (!Storage.rename(path, backup)) return false;
  } else if (Storage.exists(path) && !Storage.remove(path)) {
    return false;
  }
  return Storage.rename(temp, path);
}

void DashboardActivity::pollDownload() {
  auto& t = *transfer;
  if (millis() - t.started >= TRANSFER_TIMEOUT_MS || WiFi.status() != WL_CONNECTED) {
    stopTransfer(StrId::STR_DASHBOARD_FAILED);
    return;
  }
  auto* stream = t.http.getStreamPtr();
  const int available = stream->available();
  if (available > 0) {
    const size_t count = std::min({static_cast<size_t>(available), sizeof(t.chunk), t.expected - t.received});
    const int read = stream->read(t.chunk, count);
    if (read <= 0 || t.file.write(t.chunk, read) != read) {
      stopTransfer(StrId::STR_DASHBOARD_FAILED);
      return;
    }
    t.received += read;
  } else if (!t.http.connected()) {
    stopTransfer(StrId::STR_DASHBOARD_FAILED);
    return;
  }
  if (t.received != t.expected) return;
  t.file.flush();
  t.file.close();
  t.http.end();
  if (!promoteDownload()) {
    stopTransfer(StrId::STR_DASHBOARD_FAILED);
    return;
  }
  if (++downloadPage <= dashboard::PAGE_COUNT) {
    state = State::StartDownload;
    return;
  }
  stopTransfer(StrId::STR_DASHBOARD_UPDATED);
}

void DashboardActivity::stopTransfer(StrId message) {
  transfer.reset();
  if (usedWifi) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
  state = State::Showing;
  notice = message;
  LOG_INF("DASH", "Refresh ended; free heap=%u, largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  requestUpdate();
}

void DashboardActivity::loop() {
  RenderLock lock;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == State::StartDownload || state == State::Downloading) {
      stopTransfer(StrId::STR_DASHBOARD_CANCELLED);
    } else {
      onGoHome();
    }
    return;
  }
  switch (state) {
    case State::NeedConfig:
      configure();
      return;
    case State::NeedWifi:
      connect();
      return;
    case State::StartDownload:
      startDownload();
      return;
    case State::Downloading:
      pollDownload();
      return;
    case State::Showing:
      break;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, CONFIG_HOLD_MS)) {
    configure();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    state = baseUrl[0] ? State::NeedWifi : State::NeedConfig;
    notice = StrId::STR_DASHBOARD_REFRESHING;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    page = page == 1 ? dashboard::PAGE_COUNT : page - 1;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
             mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    page = page == dashboard::PAGE_COUNT ? 1 : page + 1;
    requestUpdate();
  }
}

void DashboardActivity::render(RenderLock&&) {
  renderer.clearScreen();
  char path[64];
  cachePath(path, sizeof(path), page);
  if (!fileReady(path)) cachePath(path, sizeof(path), page, ".old");
  bool drawn = false;
  if (fileReady(path) && Storage.openFileForRead("DASH", path, imageFile) &&
      bitmap.parseHeaders() == BmpReaderError::Ok) {
    drawn = renderer.drawBitmap1Bit(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  }
  imageFile.close();
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!drawn) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   tr(STR_DASHBOARD));
    UITheme::drawCenteredWrappedText(
        renderer,
        Rect{metrics.contentSidePadding, metrics.headerHeight,
             renderer.getScreenWidth() - 2 * metrics.contentSidePadding, renderer.getScreenHeight() / 2},
        UI_12_FONT_ID, tr(STR_DASHBOARD_EMPTY), 4);
  }
  if (notice != StrId::STR_NONE_OPT) {
    const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - 28;
    renderer.fillRect(0, y, renderer.getScreenWidth(), 28, false);
    UITheme::drawCenteredText(renderer, Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                              UI_10_FONT_ID, y, I18n::getInstance().get(notice));
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DASHBOARD_REFRESH), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
