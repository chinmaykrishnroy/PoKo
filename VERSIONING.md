# Versioning & Release Guide

This document describes the versioning convention, tagging workflow, and release process for **Poko** firmware.

---

## 1. Version Scheme

Poko uses [Semantic Versioning 2.0.0](https://semver.org/): **`vMAJOR.MINOR.PATCH`**

| Segment | When to bump | Examples |
|---------|-------------|---------|
| **MAJOR** | Breaking hardware abstraction change, complete UI overhaul, incompatible NVS schema change | `v2.0.0` |
| **MINOR** | New feature, new app, new API endpoint, significant behaviour change | `v1.3.0` |
| **PATCH** | Bug fix, visual polish, documentation update, no new features | `v1.2.1` |

> **Rule of thumb**: if a user has to re-learn how to use the device → MAJOR. If they get something new → MINOR. If something broken now works → PATCH.

---

## 2. Where the Version Is Defined

The canonical version string lives in **`InfoApp.h`** inside `buildRows()`:

```cpp
rows.push_back({"Firmware",   "v1.2.0",  pokoClrCyan()});
```

Update this string before tagging. It is displayed live in the System Info app.

Optionally mirror it in `Poko.ino` as a `#define` for use in OTA metadata:

```cpp
#define POKO_FW_VERSION "v1.2.0"
```

---

## 3. Tagging a Release

### 3.1 Pre-flight Checks

```powershell
# 1. Make sure all changes are committed
git status

# 2. Compile cleanly
arduino-cli compile -b "esp32:esp32:esp32s3:FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" --output-dir ./build .

# 3. OTA flash to the device and verify
curl.exe -F "update=@./build/Poko.ino.bin" http://192.168.0.4/ota/upload
curl.exe -s "http://192.168.0.4/api/health"
# Confirm uptime_ms is small (device rebooted with new firmware)
```

### 3.2 Update Version String

Edit `InfoApp.h`:
```cpp
rows.push_back({"Firmware",   "v1.X.Y",  pokoClrCyan()});
```

### 3.3 Update CHANGELOG.md

Add a new section at the top (under the `## Unreleased` header if one exists) with:
- Date in `YYYY-MM-DD` format
- `### Added`, `### Fixed`, `### Changed`, `### Removed` sub-sections (only include non-empty ones)

### 3.4 Commit, Tag, Push

```powershell
# Stage everything
git add -A

# Commit with a clear message (no AI attribution)
git commit -m "vX.Y.Z - Short description of what changed"

# Annotated tag (preferred — stores tagger, date, and message in git history)
git tag -a vX.Y.Z -m "vX.Y.Z - Short description"

# Push commit and tags together
git push origin main --tags
```

> Use **annotated tags** (`-a`) not lightweight tags — they carry metadata and show up properly in GitHub Releases.

---

## 4. GitHub Release (Optional)

After pushing the tag, go to **GitHub → Releases → Draft a new release**:

1. Select the tag you just pushed.
2. Set the release title to `vX.Y.Z`.
3. Paste the relevant `CHANGELOG.md` section as the description.
4. Attach the compiled binary: `./build/Poko.ino.bin` (rename to `Poko-vX.Y.Z.bin` for clarity).
5. Publish the release.

---

## 5. Hotfix / Patch Workflow

For urgent bug fixes on a released version:

```powershell
# Branch from the tag
git checkout -b hotfix/v1.2.1 v1.2.0

# Fix, commit
git commit -m "v1.2.1 - Fix <issue>"

# Tag the hotfix
git tag -a v1.2.1 -m "v1.2.1 - Fix <issue>"

# Merge back to main
git checkout main
git merge hotfix/v1.2.1
git push origin main --tags
```

---

## 6. Version History Quick Reference

| Version | Date | Highlights |
|---------|------|-----------|
| v1.0.0 | 2026-09-20 | Initial release: launcher, all apps, dual-button combos, dark/light themes |
| v1.1.0 | 2026-09-26 | Safe Mode, OTA, REST API, screen-wake on plug-in/OTA, TWDT, crash recovery |
| v1.2.0 | 2026-09-28 | Themed loading screens, real-time InfoApp, high-contrast light theme, button bug fixes |

---

## 7. NVS Schema Compatibility

If a firmware update changes the NVS keys or value types, document it here and provide a migration note in the changelog. Users upgrading from an incompatible version may need to wipe NVS via the Settings app (hold L+R for 5 s).

Current NVS keys (as of v1.2.0):

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `brightness` | int | 80 | Display brightness (0–100) |
| `volume` | int | 70 | Master volume limit (0–100) |
| `amp_boost` | int | 0 | ES8311 amp boost in dB |
| `gallery_timer` | int | 0 | Slideshow auto-advance interval (0 = manual) |
| `snap_auto` | bool | true | SSync auto-connect on start |
| `theme` | int | 0 | 0 = dark, 1 = light |
| `crash_count` | int | 0 | Consecutive panic counter for Safe Mode |
