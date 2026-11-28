# Working on Kobrowse

Before changing the project, read `README.md` and `doc/plan.md`. This file
adapts applicable principles from `Mutaracha/kobalt_ru/AGENTS.md` to this
repository: do not transfer Kobalt's platform commands without verification.
When changing the build or CI, update the instructions and package checks in
the same change.

## Scope and environment

- Target **Kobo and Qt 5 only**: a Nickel file-manager plugin. KOReader and
  user-provided images are functional and visual references, not a request to
  port an Android UI. Adapt appearance and controls for e-ink.
- `src/kobrowse.cc` is the current implementation; `res/nm_kobrowse` provides
  NickelMenu integration; `doc/plan.md` is the development plan.
- `NickelHook` is a Git submodule of `pgaskin/NickelHook`, pinned by the parent
  repository. Use `git submodule update --init --recursive`; do not edit the
  dependency for project changes or update its revision without justification
  and verification.
- The primary build uses the pinned kobuild Qt 5/GCC 11 SDK with the existing
  Qt 5 NickelTC sysroot; it does not update the device's Qt. Keep Qt 5 API
  compatibility and C++11 unless a deliberate change is justified. Do not build
  Qt or repair its SDK inside the application. Private Nickel APIs remain
  firmware-sensitive even when compilation succeeds.
- Do not assume Docker, a container socket, Qt, or a real device exists in the
  Arena environment. Network and tools vary; report what was actually tested.

## Build, checks, and CI

- For quick checks without Docker (Python 3 and `make` required), run
  `python3 -m unittest discover -s tests -v`. These test packaging with a fake
  library and **do not compile** the application. With Qt 5 available, run
  `bash tests/run-file-policy-tests.sh` for real listing and path-policy code,
  `bash tests/run-pagination-ui-tests.sh` for offscreen whole-row pagination,
  `bash tests/run-settings-ui-tests.sh` for the actual settings window in an
  isolated temporary tree, `bash tests/run-brightness-tests.sh` for gestures and
  the mock brightness API, and `bash tests/run-zip-tests.sh` for ZIP creation
  and validation/extraction checks using Python `zipfile` (Qt 5 and zlib
  required). For FAT protection checks, run `bash tests/run-fat-policy-tests.sh`;
  this requires dosfstools, Qt 5, `pkg-config`, passwordless `sudo`, and the
  ability to mount a temporary loop image. The script only uses its own isolated
  image, never devices.
- For local cross-compilation when Docker and the SDK are available, run
  `bash scripts/build-kobo.sh` after initializing the submodule. It uses the
  pinned kobuild Qt 5 image, builds in an isolated temporary copy, verifies the
  archive and records provenance and hashes in `build/build-info.json`. CI
  builds this SDK only. `CONTAINER_ENGINE` and `KOBROWSE_SDK_IMAGE` permit
  deliberate alternative-environment testing. Never run `make package` alone
  and present it as a verified build. Do not build Qt.
- Run `bash tests/run-native-keyboard-tests.sh` and
  `bash tests/run-sleep-lifecycle-tests.sh` with Qt 5 for isolated mock Nickel
  keyboard and sleep lifecycle tests. These and ELF inspection cannot establish
  ABI, e-ink handoff or device compatibility; see `doc/sleep-lifecycle.md`.
- `libkobrowse.so` is generated; do not keep it in the repository. Never present
  an old binary as a new build. Do not blindly run `make clean`: NickelHook's
  standard rules can delete `KoboRoot.tgz` in the working directory.
- GitHub CI is the primary way to verify changes because the local environment
  may lack Qt, SDK, or Docker. The user has preauthorized CI tests; do not ask
  again. Necessary stage, commit, and push operations are authorized **only on
  the current working branch** to run CI. This is not permission to publish a
  release, merge, or push to another branch. Wait for results and report what
  was actually checked.
- `.github/workflows/kobo-test.yml` fetches the submodule, runs tests, builds and
  verifies the package, and stores it with metadata in the
  `kobrowse-kobo-qt5-test` artifact. The user does not need Docker or the SDK
  on their computer; the GitHub runner uses Docker. Do not promise a successful
  build before verifying the specific run. The artifact is a test build, not a
  published release.
- The test `build/KoboRoot.tgz` must contain the library, menu entry,
  `.adds/kobrowse/uninstall` (whose removal triggers uninstallation), and
  `.adds/kobrowse/KOBALT-LICENSE.txt` for adapted Kobalt code;
  `scripts/check-package.py` checks contents, ownership, modes, and ELF type.
  Do not package user configuration or other applications.
- CI and automated tests do not prove operation on a physical Kobo. Separately
  verify firmware, installation, interface, USB, and sleep on the device;
  recommend a backup before testing.

## Data safety

- Preserve user files, settings, and other modifications. Do not overwrite a
  user's `KoboRoot.tgz` or `kobrowse.ini` without permission;
  `build/KoboRoot.tgz` is an overwriteable build output. Do not treat
  `/mnt/onboard` as a protected sandbox: symbolic links, archives, and file
  operations require separate checks. File-operation tests must use isolated
  temporary directories, never user data.
- When changing persisted formats, ensure backward compatibility and migration
  tests. Do not remove settings or user data to clear caches or update a
  version. File operations must report errors and not silently lose data.
- The plugin runs inside Nickel and may remain active during a USB session:
  do not hold onboard storage files open unnecessarily, avoid excess resource
  use, and treat private Nickel APIs cautiously.

## Workflow

- First assess duplicated builds, unnecessary rebuilds, SDK workarounds, and
  unclear ownership; prefer a simple solution rather than copying Kobalt's
  entire infrastructure. Do not depend on other working copies.
- Keep documentation in `doc/` except the root `README.md` and `AGENTS.md`.
  Respect user edits, do not delete others' data, and do not run visible UI
  tests without permission. After work, stop your auxiliary processes and clean
  up your temporary directories and containers without disturbing others'
  processes, SDKs, or useful results.
- Outside the CI authorization above, do not stage, commit, push, or publish
  without explicit user permission. Use the current branch only. Do not add an
  agent coauthor; warn in advance if a signed operation might prompt 1Password.
  In commit messages, write `alpha` and `beta` in lowercase; `RC` may stay uppercase.
