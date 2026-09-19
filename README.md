# MLB scoreboard for reMarkable

A live baseball scoreboard that runs as a window inside the reMarkable UI.
Pick any of the 30 clubs; see the score, count, runners, matchup, the pitch that
was just thrown, and the line score — plus a grid of every game on today.

Data comes from MLB's public Stats API. No key, no account.

---

## What it needs

- A reMarkable with **developer mode** enabled
- **[xovi](https://github.com/asivery/xovi)** and
  **[AppLoad](https://github.com/asivery/rm-appload)** installed — this is an
  AppLoad app and will not run without them
- Built and tested on a **Paper Pro Move** (codename `chiappa`, 954x1696) on
  Codex Linux 5.7.126 / Qt 6.8.2. It should work on a Paper Pro (`ferrari`,
  1620x2160) — the layout derives from the panel rather than assuming a size —
  but that is untested.

## Install

Grab the zip from [Releases](../../releases), unzip it, and:

```bash
RM_HOST=10.11.99.1 ./install.sh
ssh root@10.11.99.1 'systemctl restart xochitl'
```

`10.11.99.1` is the tablet over USB; over wifi use the address in
Settings → About. The restart is required — AppLoad only reads app manifests and
icons when xochitl starts.

Then: sidebar → AppLoad → **MLB**.

## Using it

| Gesture | What happens |
|---|---|
| Tap a game card | Opens that game |
| Tap the top strip | Shows **BACK** and **TEAM** |
| **TEAM** | Pick which club to follow |
| Swipe down from the top | AppLoad's own bar: minimize / maximize / close |

Tapping anywhere else does nothing, deliberately — it is easy to brush an
e-ink screen.

The app ships with a neutral icon. Choosing a team writes that club's logo over
it, which the launcher picks up on the next restart. Your choice is saved to
`/home/root/.config/scoreboard/settings.json`, outside the app directory, so
reinstalling does not forget it.

## Building

You need Docker and the reMarkable SDK matching your device's OS version, from
[developer.remarkable.com/links](https://developer.remarkable.com/links)
(~460MB). Check the version with
`ssh root@10.11.99.1 'grep ^VERSION= /etc/os-release'`.

```bash
./package.sh ~/Downloads/remarkable-production-image-5.7.119-ferrari-public-x86_64-toolchain.sh
```

That cross-compiles the backend, lints the QML, builds `resources.rcc`, and
writes `dist/mlb-scoreboard.zip`.

**Build against an SDK at or below your device's OS version.** Qt keeps ABI
compatibility forward, not backward: a binary built against Qt 6.10 will not
start on a device running 6.8.2, and the only symptom is
`version 'Qt_6.10' not found`.

The SDK only runs on x86_64 Linux, so on an Apple Silicon Mac the first build
runs under emulation and takes 10–20 minutes. It is cached after that.

## How it is put together

```
src/          backend: a headless Qt console process
  GameFeed      polls the Stats API, flattens everything into one state map
  LogoStore     fetches team logos once, caches them greyscale
  AppLoadLink   AppLoad's frontend/backend socket protocol
appload-native/
  ui/*.qml      the interface, loaded into xochitl by AppLoad
  manifest.json AppLoad app definition
```

The split is not decorative. The QML runs **inside xochitl**, so it inherits
xochitl's OpenSSL policy — which blocks `statsapi.mlb.com` (see below). The
backend is a separate process with its own environment, which is what lets it
reach the network at all. The two talk over a unix socket AppLoad creates.

### The TLS thing

reMarkable ships `/etc/ssl/openssl.cnf.d/10-reduce-tls-ciphers.cnf`, which
restricts TLS 1.2 to ECDHE-**ECDSA** suites for SOG-IS / EU-RED compliance. MLB
serves an **RSA** certificate and does not support TLS 1.3, so there is no
overlap and the handshake fails with `tls alert handshake failure`.

`install.sh` puts a small per-process OpenSSL config on the device and the
backend points `OPENSSL_CONF` at it, restoring the default cipher list for this
one process. System-wide TLS policy is untouched.

Busybox `wget` works on the device because it has its own TLS stack — so do not
use it to conclude the network is fine.

If you are building something else for these tablets, the reusable half of what
this took is written up in [docs/PLATFORM.md](docs/PLATFORM.md): SDK version
matching, AppLoad packaging and its startup caching, the socket protocol, the
TLS workaround, rotation, and what e-ink does to a design.

## Notes

- Nothing is stopped or replaced. The app is a window; closing it returns you to
  your notebook immediately.
- No animations anywhere, deliberately. E-ink smears.
- The tablet deep-sleeps roughly 40 seconds after you stop touching it, and
  nothing runs while it is suspended. Scores resume on wake and refresh within
  one 15-second poll.
- The Stats API is undocumented and can change without notice.

## Licence

[MIT](LICENSE).

The code and the drawn launcher icon are covered by it. Team names and logos
are not ours to license: they are fetched from MLB at runtime onto the device
that displays them, and none are redistributed here.
