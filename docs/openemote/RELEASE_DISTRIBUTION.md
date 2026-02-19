# OpenEmote Release Distribution and Hosting

This document defines how we build and distribute OpenEmote desktop binaries.

## Goals

1. Keep GitHub Releases as canonical artifact source.
2. Support Windows, macOS, and Linux package outputs.
3. Make `openemote.com/download` a thin index/router over signed release assets.
4. Allow optional GCS mirroring without coupling build success to mirror availability.

## Current build outputs

From `.github/workflows/build.yml`:

1. Windows zip bundle:
   - `chatterino-windows-x86-64-Qt-<qt>.zip`
2. Windows symbols:
   - `chatterino-windows-x86-64-Qt-<qt>-symbols.pdb.7z`
3. macOS DMG:
   - `chatterino-macos-Qt-<qt>.dmg`
4. Linux DEB:
   - `Chatterino-ubuntu-22.04-Qt-<qt>.deb`
   - `Chatterino-ubuntu-24.04-Qt-<qt>.deb`

## Release channels

1. Nightly:
   - Built from `master`
   - Published as prerelease on tag `nightly-build`
2. Stable:
   - Use semantic release tags
   - Run workflow with stable build mode and publish release assets

## Build type controls (GitHub Actions)

`build.yml` supports `workflow_dispatch` input `buildType`:

1. `auto`:
   - `v*` exact tag => stable mode
   - otherwise => nightly mode
2. `nightly`:
   - Forces nightly build metadata
3. `stable`:
   - Forces stable build metadata

## Hosting strategy

1. Canonical:
   - GitHub Releases assets and checksums
2. Web presentation:
   - `openemote.com/download` serves latest metadata and platform links
3. Optional mirror:
   - Push release assets + checksums to GCS bucket for CDN and archival
   - Keep GitHub URLs as fallback links in metadata

## Suggested metadata schema for `openemote.com/download`

```json
{
  "channel": "nightly",
  "version": "nightly-build",
  "published_at": "2026-02-18T00:00:00Z",
  "artifacts": [
    {
      "platform": "windows-x86_64",
      "name": "chatterino-windows-x86-64-Qt-6.7.1.zip",
      "sha256": "<hex>",
      "url_github": "<github-release-url>",
      "url_mirror": "<gcs-url-optional>"
    }
  ]
}
```

## Operational notes

1. Keep artifact naming stable for update tooling.
2. Publish checksums per release.
3. Do not delete past stable artifacts; deprecate via metadata flags.
4. Prefer additive CI changes over replacing existing upstream-compatible flows.
