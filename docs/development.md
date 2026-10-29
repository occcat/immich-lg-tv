# Development

## Desktop preview

macOS:

```sh
brew install sdl2-compat sdl2_ttf sdl2_image ffmpeg pkgconf
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
make host
```

Ubuntu 24.04:

```sh
sudo apt-get install build-essential pkg-config libsdl2-dev libsdl2-image-dev \
  libsdl2-ttf-dev libcurl4-openssl-dev libavformat-dev libavcodec-dev libavutil-dev ffmpeg
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
make host
```

Start `make demo` in another terminal, then launch the public fixture account:

```sh
build/immich-lg-tv --server http://127.0.0.1:22830 \
  --api-key immich-tv-demo --allow-http
```

Use the app's connection screen for real API keys. Desktop video preview uses
an external `ffplay` window; the TV build uses the in-app native hardware player.

## Tests and screenshots

```sh
make fixtures FIXTURE_ARGS=--offline
make test
.venv/bin/python tools/check-repository.py
.venv/bin/python tools/capture-screenshots.py
```

`make test` runs the native C policy/configuration suite and Python API, demux,
AAC/Annex B, pagination, navigation, pointer, and pixel-alignment regressions.
`capture-screenshots.py` launches the real SDL QA binary against its own temporary
fixture server, checks the resulting states, and saves five screens and provenance
under `.local/screenshots/desktop/`.

For public demo photography, use `make fixtures` without `--offline`.
Fixtures are ignored by Git and excluded from the application package.

`make QA=1 host` and `make QA=1 ipk` build separate test binaries. QA launch
scripts and capture parameters are unavailable in the production executable.
Always rebuild with `make ipk` before distributing a package.

## webOS IPK

The checksum-pinned NDK installer supports macOS Apple Silicon, macOS Intel,
and Linux ARM64. Linux x86_64 can run host tests but is not a supported NDK
installer host in this repository.

```sh
python3 tools/setup-sdk.py
make ipk
python3 tools/verify-release.py
```

The package is `dist/com.immichtv.viewer_0.1.0_arm.ipk`. `project.json` defines
its identity; the native `APP_ID` and `APP_VERSION` are checked against it.
The stable application ID preserves compatibility with earlier installations.

The IPK dynamically links replaceable LGPL FFmpeg libraries. The complete
corresponding source archive and original build configuration accompany it in
`dist/`. Distribute these together; see [NOTICE.md](../NOTICE.md).

## Manual GitHub Actions

Open **Actions → Manual CI → Run workflow**. Leave `build_tv` unchecked to run
Linux native tests and save SDL screenshots. Check it to additionally build the
ARM IPK on macOS and upload the package with its FFmpeg sources.

From an authenticated GitHub CLI:

```sh
gh workflow run ci.yml --repo occcat/immich-lg-tv -f build_tv=true
```

The only workflow event is `workflow_dispatch`. Pushes, pull requests, tags,
and schedules do not trigger CI. Workflow actions use pinned commit SHAs and
read-only repository permissions.

## Commits

Follow [AGENTS.md](../AGENTS.md). Every new commit has an English Conventional
Commits subject, a blank line, and a detailed body describing changes and reasons.
Test reports and task-status commentary belong outside the commit message.

The initial local prototype history predates this policy and is retained without
rewriting. The policy applies to all new work.
