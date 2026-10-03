# Personal X3 firmware

This branch (`x3-personal`) is lxol's MIT-licensed CrossPoint fork. The base is
upstream **1.6.5**, commit `93e98bb7`. The old `x3-port` branch remains available.
The normal reader is retained, with a Home → Dashboard entry for three static
image pages. This is a personal extension outside upstream's reader-only scope.

## Dashboard setup and controls

1. On the image server, publish three files named `page-1.bmp`, `page-2.bmp`,
   `page-3.bmp` in a directory reachable from the reader's Wi-Fi network.
2. Open **Dashboard** on the Home screen. Enter the directory URL, for example
   `http://dashboard.local:8090/x3`. The address is saved on SD in
   `/dashboard-url.txt`. You can instead upload that plain-text file through
   File Transfer. No personal hostnames or credentials are built into firmware.
3. Press **Confirm / Refresh**, select your Wi-Fi network, and wait for the pages
   to download. Wi-Fi switches off afterward.
4. Use **Left/Right** or the page-turn buttons to change pages. **Back** cancels a
   running download; otherwise it returns Home. Hold **Confirm** for one second
   to change the server address.

Cached images work offline in `/.crosspoint/dashboard/`. Opening the dashboard
does not automatically refresh. The image's **Updated** timestamp shows its age;
the displayed clock is a snapshot, not a live clock. Normal auto-sleep still
applies. Timed wake/refresh is intentionally not implemented in this revision.
The dashboard uses portrait orientation and restores the prior orientation on
exit. After any Wi-Fi session, returning Home uses upstream's silent reboot to
reclaim network heap before reading.

The initial server is the existing private `kobo-dash` project. Its `server/xteink.py`
renders compact 528×792 pages from the same Plane data snapshot as the Kobo.
The device sees only images; Plane credentials remain on the server.

## Image contract and resource limits

- HTTP, status 200, known Content-Length, no redirects, no authentication.
  Use this on a trusted local Wi-Fi network. HTTPS/query-string URLs are rejected.
- Windows BMP: 40-byte DIB header, pixel offset 62, uncompressed 1 bpp,
  bottom-up rows, black/white palette, exact portrait screen dimensions.
  On X3 this is **53,918 bytes per page**. A Kobo-sized image is rejected.
- Headers and complete length are checked before cache promotion. A `.tmp`
  download cannot replace a working page. The previous valid page is retained
  as `.old`, including across an interrupted FAT rename.
- Socket operations time out after 3 seconds; each body transfer has a 12-second
  deadline. Back is polled between body chunks. Header/connect calls may delay
  cancellation by their socket timeout. Refresh stops on the first error; pages
  already refreshed remain available, while remaining pages retain their cache.
- No second framebuffer or background task. The activity owns its small palette,
  URL and state. A fallible transfer allocation owns HTTPClient, WiFiClient and
  a reusable 256-byte chunk only while refreshing. The existing bitmap decoder
  allocates a 200-byte row scratch buffer on X3, not a full image.
- Heap logs at the end of every refresh report free heap and largest block.
  Device testing must check both across repeated refreshes and return to reading.

## Build and update

```sh
source .venv/bin/activate  # this workspace's pinned Python 3.13 / pioarduino environment
git submodule update --init --recursive
pio run -e gh_release
python3 scripts/package_personal.py
```

The upstream build uses pioarduino 6.1.19 for both the outer and nested toolchain;
`.github/workflows/personal-x3.yml` documents installation and produces a firmware
artifact for every push to this branch. The binary is an **application image**,
not a whole-flash backup. Do not flash it at address zero.

Keep the virtual environment's `bin` directory first in PATH for the entire
build, including nested builds. The system Python/PlatformIO combination on this
machine failed with `SCons.Tool.FortranCommon`; the isolated pinned environment
avoids mixing system and downloaded SCons packages.

For an existing compatible CrossPoint installation, copy the application `.bin`
to the SD card over File Transfer, then use **Settings → Update from SD**.
Keep power connected during the update. On older CrossPet installations, verify
the partition table/OTA support first; preserve the current flash and SD data
before migrating. A stock full-flash backup restores stock firmware, not the
current CrossPet settings or reading progress.

Personal versions are `1.6.5-lxol.1`, `1.6.5-lxol.2`, etc. The OTA checker reads
only `lxol/crosspoint-reader` releases and accepts only that version pattern.
It compares the upstream version and then the personal revision. Plain upstream
versions are refused. Releases must name the application asset exactly
`crosspoint-<tag>-x3-x4.bin`, without a leading `v` in the tag. Keep untested builds
as drafts; publish a stable release only after testing it on the X3.

## Bringing in upstream changes

```sh
git fetch upstream --tags
git switch x3-personal
git switch -c integrate/next-stable
git merge <selected-upstream-tag>
git submodule update --init --recursive
```

Keep the personal version suffix, release feed, Home menu entry and dashboard
module when resolving conflicts. Update the base version in the package manifest,
then run the host tests, formatter and a release build. Test on the X3 before
merging back into `x3-personal`. Avoid pulling upstream master straight into the
device branch. Individual relevant fixes can be cherry-picked instead.

## Verification before the first stable release

- Open a Russian EPUB, advance pages and check progress after sleep/wake.
- With no dashboard images cached, open Dashboard and return Home before
  refreshing; neither the empty screen nor its exit should crash.
- Open Dashboard, configure the directory, refresh and navigate all three pages.
- Disable the server and refresh: a bounded failure must preserve cached pages.
- Cancel Wi-Fi selection and a body transfer; return Home and reopen a book.
- Check repeated refreshes for falling heap / largest allocation; target >50 KB
  free heap after refresh and no panics in serial logs.
- Enter from each reader orientation; verify Dashboard stays portrait and the
  normal reader retains its orientation afterward.
- Verify auto-sleep and power-button sleep both work after a refresh.

Host validation covers URL/version parsing and malformed image headers. Server
tests cover empty/long/missing data and the exact BMP contract. Neither substitutes
for the physical device checks above.
