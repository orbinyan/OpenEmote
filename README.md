<img src="resources/icon.png" width="64" height="64" alt="OpenEmote icon" />

# OpenEmote

OpenEmote is a fast, Twitch-first chat client based on Chatterino.

[![Build](https://github.com/orbinyan/OpenEmote/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/orbinyan/OpenEmote/actions/workflows/build.yml)

## Upstream

This repository is intentionally not a GitHub "fork" object. We track upstream
work from:

- Chatterino: https://github.com/Chatterino/chatterino2
- SevenTV: https://github.com/SevenTV/chatterino7

## Docs

- [Open core policy](docs/openemote/OPEN_CORE_POLICY.md)
- [Release cadence](docs/openemote/RELEASE_CADENCE.md)
- [Release distribution + hosting](docs/openemote/RELEASE_DISTRIBUTION.md)
- [Release + hosting notes](docs/openemote-release.md)
- [Testing notes](docs/openemote-testing.md)
- [Self-hosted quickstart](docs/openemote/self-hosted-quickstart.md)
- [Contributor notes for OpenEmote maintainers](docs/openemote/CONTRIBUTING_NOTES.md)
- [Contributing](CONTRIBUTING.md)

## Download

OpenEmote builds are published from this repository's Actions and Releases.

- Nightly channel: GitHub tag `nightly-build` (auto-updated by CI from `main`)
- Stable channel: semantic tags (for example `v2.5.4-openemote.1`) and release workflow dispatch

## Nightly build

You can download the latest OpenEmote nightly build here:

- https://github.com/orbinyan/OpenEmote/releases/tag/nightly-build

You might also need to install the [VC++ Redistributables](https://aka.ms/vs/17/release/vc_redist.x64.exe) from Microsoft if you do not have it installed already.  
If you still receive an error about `MSVCR120.dll missing`, then you should install the [VC++ 2013 Restributable](https://download.microsoft.com/download/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x64.exe).

## Building

To get source code with required submodules run:

```shell
git clone --recurse-submodules https://github.com/orbinyan/OpenEmote.git
```

or

```shell
git clone https://github.com/orbinyan/OpenEmote.git
cd OpenEmote
git submodule update --init --recursive
```

- [Building on Windows](BUILDING_ON_WINDOWS.md)
- [Building on Windows with vcpkg](BUILDING_ON_WINDOWS_WITH_VCPKG.md)
- [Building on Linux](BUILDING_ON_LINUX.md)
- [Building on macOS](BUILDING_ON_MAC.md)
- [Building on FreeBSD](BUILDING_ON_FREEBSD.md)

## OpenEmote release artifacts (current CI)

From `.github/workflows/build.yml`:

- Windows: `chatterino-windows-x86-64-Qt-<qt>.zip`
- Windows symbols: `chatterino-windows-x86-64-Qt-<qt>-symbols.pdb.7z`
- macOS: `chatterino-macos-Qt-<qt>.dmg`
- Linux: `Chatterino-ubuntu-22.04-Qt-<qt>.deb`, `Chatterino-ubuntu-24.04-Qt-<qt>.deb`

Release behavior today:

- `main` pushes produce/update nightly prerelease artifacts (`nightly-build`)
- Full stable release publishing is handled by tag/release process (documented in `docs/openemote/RELEASE_DISTRIBUTION.md`)

## OpenEmote release artifacts (current CI)

From `.github/workflows/build.yml`:

- Windows: `chatterino-windows-x86-64-Qt-<qt>.zip`
- Windows symbols: `chatterino-windows-x86-64-Qt-<qt>-symbols.pdb.7z`
- macOS: `chatterino-macos-Qt-<qt>.dmg`
- Linux: `Chatterino-ubuntu-22.04-Qt-<qt>.deb`, `Chatterino-ubuntu-24.04-Qt-<qt>.deb`

Release behavior today:

- `master` pushes produce/update nightly prerelease artifacts (`nightly-build`)
- Full stable release publishing is handled by tag/release process (documented in `docs/openemote/RELEASE_DISTRIBUTION.md`)

## Git blame

This project has big commits in the history which touch most files while only doing stylistic changes. To improve the output of git-blame, consider setting:

```shell
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

This will ignore all revisions mentioned in the [`.git-blame-ignore-revs`
file](./.git-blame-ignore-revs). GitHub does this by default.

## Code style

The code is formatted using [clang-format](https://clang.llvm.org/docs/ClangFormat.html). Our configuration is found in the [.clang-format](.clang-format) file in the repository root directory.

For more contribution guidelines, take a look at [the wiki](https://wiki.chatterino.com/Contributing%20for%20Developers/).

For OpenEmote contributors, we also mirror key maintainer rules in
[`docs/openemote/CONTRIBUTING_NOTES.md`](docs/openemote/CONTRIBUTING_NOTES.md)
so the fork stays aligned with upstream expectations.

## Doxygen

Doxygen is used to generate project information daily and is available [here](https://doxygen.chatterino.com).
