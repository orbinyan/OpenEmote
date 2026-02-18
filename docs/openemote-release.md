# OpenEmote Build and Release

This repository keeps the upstream Chatterino build system, then adds an
OpenEmote-focused release path for Linux, macOS, and Windows.

## Custom provider model

OpenEmote custom providers can now be configured as an ordered list of API
base URLs (`/emotes/custom/apiBaseUrls`).

- Providers are queried in listed order.
- Emote code collisions resolve in favor of later providers.
- The legacy single URL setting (`/emotes/custom/apiBaseUrl`) is still used as
  fallback for backward compatibility.

## Local build (Linux / Nix)

```bash
nix --extra-experimental-features 'nix-command flakes' develop -c cmake --preset release
nix --extra-experimental-features 'nix-command flakes' develop -c cmake --build --preset release -j"$(nproc)"
```

Binary output:

- `build/release/bin/chatterino`

## Local build presets

`CMakePresets.json` provides:

- `dev`
- `release`
- `macos-release`
- `windows-release`

Examples:

```bash
cmake --preset dev
cmake --build --preset dev -j
```

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

## CI pipeline

Workflow: `.github/workflows/openemote-ci.yml`

- Linux: Nix build, uploads `openemote-linux-x86_64.tar.gz`
- macOS: Qt + brew deps, builds DMG, uploads `openemote-macos.dmg`
- Windows: Conan + MSVC + windeployqt, uploads `openemote-windows-x86_64.zip`

## Release pipeline

Workflow: `.github/workflows/openemote-release.yml`

- Trigger: push a tag matching `v*-openemote.*`
- Builds release artifacts on Linux/macOS/Windows
- Publishes a GitHub prerelease with:
  - `openemote-linux-x86_64.tar.gz`
  - `openemote-macos.dmg`
  - `openemote-windows-x86_64.zip`
  - `sha256-checksums.txt`

Example release tag:

- `v2.6.0-openemote.1`

## Credential storage safety

Credentials are stored in keychain backends by default.

- Linux: libsecret/KWallet/GNOME keyring (toggle in settings)
- macOS: system keychain
- Windows: credential manager via QtKeychain backend

OpenEmote hardening behavior:

- If keychain usage is disabled or unavailable, plaintext credential storage is
  refused by default.
- Users can explicitly opt in from Settings:
  - `Allow insecure plaintext credential storage (not recommended)`
- For explicit debugging/automation only, insecure fallback can also be enabled
  with:
  - `CHATTERINO_ALLOW_INSECURE_CREDENTIALS=1`
- Both opt-in paths emit runtime warnings.

This prevents accidental token persistence in `credentials.json`.
