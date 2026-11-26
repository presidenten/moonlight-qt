# Moonlight (Metal fork) — agent notes

## Lineage
- We ship **Andy Grundman's Metal fork**: `andygrundman/moonlight-qt`, branch
  `andyg.macos-metal-frame-pacing-pyrowave2`. App is "Moonlight Metal", bundle id
  `org.hybridized.Moonlight`.
- `master` here is a snapshot of Andy's `master`, which just mirrors upstream moonlight-qt.
  It is **not** what we run. `feat/auto-connect` carries the same changes on that base and
  can be deleted once `feat/auto-connect-metal` is proven.
- Working branch: **`feat/auto-connect-metal`** = Andy's Metal branch + our commits.

## Our changes (keep these when rebasing)
- **Auto-connect** (`app/gui/computermodel.*`, `appmodel.*`, `PcView.qml`, `AppView.qml`):
  when the PC list is the active view and a paired host transitions to online (including the
  first poll after startup), open its app view and launch Desktop / direct-launch / only app.
  When that stream ends, pop back to the PC list. No UI or settings; it is always on.
- **CI signing** (`.github/workflows/build-win-mac.yml`, `build.yml`, `scripts/generate-dmg.sh`):
  macOS job imports a cert from secrets `MACOS_SIGNING_P12` / `MACOS_SIGNING_PASSWORD` and
  signs app + dmg. Self-signed cert ⇒ `CODESIGN_OPTIONS=--timestamp` (no hardened runtime,
  which needs a Team ID). Reason: ad-hoc builds cannot persist macOS Local Network permission,
  so hosts show offline when launched from Finder.
- Log lines prefixed `Auto-connect:` / "requesting auto-connect" trace the feature.

## Pulling new work from Andy
```
git remote add andy https://github.com/andygrundman/moonlight-qt.git   # if missing
git fetch andy
git rebase andy/andyg.macos-metal-frame-pacing-pyrowave2 feat/auto-connect-metal
git submodule update --init --recursive
```
Expect conflicts only in `scripts/generate-dmg.sh` (signing block) and possibly `AppView.qml`.

## Build / verify
- macOS dmg: push the branch; GitHub Actions `Build` uploads `Moonlight-macOS-<sha>` (dmg
  named `Moonlight-<sha6>.dmg`). Runner `macos-26` can queue for a long time.
- No Mac here. Compile-check in Docker (`ubuntu:24.04`, qt6 + sdl2 + ffmpeg dev packages,
  `qmake6 && make`). On the Metal branch the bundled `imgui` does not build on Linux; run
  qmake/make on `app/app.pro` alone to compile our files.
- The user pushes from their Mac; `gh` is not authenticated in this container.
- First launch of a new build on a Mac: Gatekeeper "Open Anyway" (not notarized), then one
  Local Network prompt. If hosts stay offline: `tccutil reset All org.hybridized.Moonlight`.
