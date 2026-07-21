# Native Windows Integration Implementation Plan

> **For agentic workers:** Implement this plan task-by-task — one task per commit, run each task's test before moving on. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add native Windows build, I/O, Boltsnap IPC, and simple packaging support without regressing the current Linux editor.

**Architecture:** Keep the shared Qt editor unchanged and isolate only the actual OS boundaries with `Q_OS_WIN`/CMake platform conditions. Retain the current richer Boltsnap replace/add-video protocol, use `QLocalSocket` only for its Windows transport, and ship a conventional MSI without the self-signed MSIX/COM context-menu layer.

**Tech Stack:** C++20, Qt 6 Widgets/Network/Multimedia, CMake 3.21+, Qt Test, WiX 5, GitHub Actions.

## Global Constraints

- Preserve the current Linux behavior and all 24 Linux tests.
- Run local builds and tests with at most three parallel jobs.
- Add no new runtime dependency beyond Qt Network on Windows.
- Preserve Unicode paths end-to-end on Windows.
- Do not install certificates or per-user MSIX identity packages from a per-machine MSI.
- Keep `ffmpeg`, `ffprobe`, and `tesseract` optional external tools.

---

### Task 1: Platform-aware build

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `eddy_core`, `eddy`, and Qt test targets.
- Produces: a Linux build with optional Wayland dependencies and a Windows build linked to `Qt6::Network` and `dwmapi`.

- [ ] **Step 1: Configure the existing Linux tree**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
Expected: configuration succeeds with Wayland support detected when available.

- [ ] **Step 2: Apply the minimum platform conditions**

Use `if(WIN32)` for Qt Network and `dwmapi`, `if(UNIX AND NOT APPLE)` for Wayland discovery, compiler-specific Release flags, and `qt_generate_deploy_app_script()` for Windows installs.

- [ ] **Step 3: Verify Linux still builds**

Run: `cmake --build build --parallel 3`
Expected: all existing targets build.

- [ ] **Step 4: Commit**

Run: `git commit -am "build: add platform-aware Windows configuration"`

### Task 2: Windows Boltsnap transport

**Files:**
- Modify: `src/boltsnapipc.cpp`
- Modify: `src/boltsnapipc.h`
- Modify: `tests/test_boltsnapipc.cpp`

**Interfaces:**
- Consumes: existing frame builders and `DeliverResult` response contract.
- Produces: the same `sendPngToBoltsnapShelf`, `sendPngToBoltsnapCard`, `sendVideoToBoltsnapCard`, and `sendVideoToBoltsnapShelf` API on Unix sockets and Windows Named Pipes.

- [ ] **Step 1: Add the Windows pipe-contract test**

Add a Windows-only test expecting `\\\\.\\pipe\\boltsnap-domain-a_user` for `USERDOMAIN=DOMAIN` and `USERNAME=A User`, while retaining all Unix socket tests under the non-Windows branch.

- [ ] **Step 2: Verify the test guards compile on Linux**

Run: `cmake --build build --target test_boltsnapipc --parallel 3 && ctest --test-dir build -R test_boltsnapipc --output-on-failure`
Expected: existing Linux behavior remains green before the transport edit.

- [ ] **Step 3: Implement the minimum Windows transport**

Use `QLocalSocket`, the existing frame bytes, bounded connection/write/read waits, and the existing acknowledgement parser semantics. Keep all POSIX includes and helpers inside the non-Windows branch.

- [ ] **Step 4: Re-run the focused test**

Run: `cmake --build build --target test_boltsnapipc --parallel 3 && ctest --test-dir build -R test_boltsnapipc --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git commit -am "feat: add Windows Boltsnap named-pipe transport"`

### Task 3: Unicode CLI and safe Windows file replacement

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/videoexporter.cpp`
- Modify: `tests/test_cli.cpp`
- Modify: `tests/test_videoexporter.cpp`

**Interfaces:**
- Consumes: `QCoreApplication::arguments()` and the existing same-directory video temporary file.
- Produces: Unicode-safe Windows CLI paths, binary stdin/stdout, an empty-argument file picker, and replacement of an existing video without deleting the original on failure.

- [ ] **Step 1: Add regression coverage**

Add a CLI parser case using `Grüße 日本語.png`. Add `replaceFileAtomically(const QString &from, const QString &to)` to the test's expected API and a focused test that creates both files, replaces the destination, and verifies the source disappeared and the destination contains the new bytes. The Windows implementation must use the native replace-existing operation; Linux keeps its existing atomic rename.

- [ ] **Step 2: Run focused tests before implementation**

Run: `cmake --build build --target test_cli test_videoexporter --parallel 3 && ctest --test-dir build -R 'test_(cli|videoexporter)' --output-on-failure`
Expected: the video-exporter target fails to compile because `replaceFileAtomically` does not exist yet.

- [ ] **Step 3: Implement Windows I/O boundaries**

Read arguments from `QCoreApplication::arguments().mid(1)`, set redirected stdin/stdout to binary mode, show `QFileDialog` only when Windows receives no arguments, and replace completed in-place video exports with `ReplaceFileW`.

- [ ] **Step 4: Run focused tests**

Run: `cmake --build build --target test_cli test_videoexporter --parallel 3 && ctest --test-dir build -R 'test_(cli|videoexporter)' --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git commit -am "fix: preserve Windows Unicode and video replacement"`

### Task 4: Simple Windows package and CI

**Files:**
- Create: `packaging/windows/Eddy.wxs`
- Create: `packaging/windows/License.rtf`
- Create: `packaging/windows/build-msi.ps1`
- Create: `.github/workflows/ci.yml`
- Modify: `.gitignore`
- Modify: `README.md`

**Interfaces:**
- Consumes: CMake's installed `eddy.exe` plus deployed Qt/MSVC runtime files.
- Produces: a conventional per-machine MSI with Start-menu shortcut and classic Open-with registration, plus Ubuntu/Windows build-and-test checks.

- [ ] **Step 1: Add the package without identity custom actions**

Install staged files, HKLM application capabilities, classic image associations, and a Start-menu shortcut. Do not create an MSIX, COM server, certificate, or PowerShell install/uninstall custom action.

- [ ] **Step 2: Add the build script**

Stage `eddy.exe`, run `windeployqt`, copy the MSVC runtime, and invoke a local WiX 5 tool. Fail the script on every packaging error.

- [ ] **Step 3: Add CI**

Configure Ubuntu and Windows Release builds, install Qt Multimedia/SVG, and run both build and CTest with three jobs.

- [ ] **Step 4: Validate locally available formats**

Run: `xmllint --noout packaging/windows/Eddy.wxs`
Expected: valid XML.

- [ ] **Step 5: Commit**

Run: `git add .github .gitignore README.md packaging && git commit -m "build: add simple Windows package and CI"`

### Task 5: Final regression gate

**Files:**
- Review: all changes since the baseline commit.

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces: a branch ready for the user's Linux smoke test and a Windows tester's native validation.

- [ ] **Step 1: Build everything**

Run: `cmake --build build --parallel 3`
Expected: success.

- [ ] **Step 2: Run every Linux test**

Run: `ctest --test-dir build --parallel 3 --output-on-failure`
Expected: 24/24 or more tests pass.

- [ ] **Step 3: Check patch quality**

Run: `git diff --check HEAD~4..HEAD && git status --short --branch`
Expected: no whitespace errors and a clean worktree.

- [ ] **Step 4: Cross-compile Windows-only source where possible**

Run the installed MinGW compiler over any standalone Win32 source and verify exported symbols if such a source remains. Otherwise record that the full Windows Qt/MSVC build is delegated to CI and the Windows tester.
