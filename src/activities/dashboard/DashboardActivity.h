#pragma once

#include <Bitmap.h>
#include <DashboardProtocol.h>
#include <HalStorage.h>
#include <I18n.h>

#include "activities/Activity.h"

class DashboardActivity final : public Activity {
  enum class State { Showing, NeedConfig, NeedWifi, StartDownload, Downloading };
  struct Transfer;

  State state = State::Showing;
  GfxRenderer::Orientation savedOrientation = GfxRenderer::Portrait;
  char baseUrl[dashboard::URL_CAPACITY]{};
  unsigned page = 1;
  unsigned downloadPage = 1;
  StrId notice = StrId::STR_NONE_OPT;
  bool usedWifi = false;
  bool fileReady(const char* path);
  void cachePath(char* out, size_t size, unsigned number, const char* suffix = ".bmp") const;
  bool promoteDownload();
  void configure();
  void connect();
  void startDownload();
  void pollDownload();
  void stopTransfer(StrId message);

  // Owned by the activity, not a second framebuffer. Bitmap's palette is
  // >256 bytes, so keep it off the render stack and reuse it between pages.
  HalFile imageFile;
  Bitmap bitmap;
  std::unique_ptr<Transfer> transfer;

 public:
  DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~DashboardActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state != State::Showing; }
  bool skipLoopDelay() override { return state == State::Downloading; }
};
