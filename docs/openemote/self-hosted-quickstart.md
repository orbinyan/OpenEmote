# OpenEmote self-hosted quickstart (free path)

This guide is for streamers who want full control and a fully open path.

## What you host

- A static `manifest.json` that follows `openemote-pack-v1.schema.json`.
- Static image assets referenced by the manifest.
- Optional `badges.json` and `paints.json`.

## Requirements

- Any static host with HTTPS.
- A domain (recommended) or subdomain.
- A way to upload files (scp, rsync, S3 sync, GitHub Pages deploy, etc.).

## 1) Create pack folder

```bash
mkdir -p openemote-pack/assets
cd openemote-pack
```

## 2) Add sample manifest

Save as `manifest.json`:

```json
{
  "pack_id": "my-stream-pack",
  "name": "My Stream Pack",
  "version": 1,
  "updated_at": "2026-02-17T00:00:00Z",
  "license": "CC-BY-NC-4.0",
  "emotes": [
    {
      "id": "hype_01",
      "name": "HypeWave",
      "flags": {
        "animated": false,
        "zero_width": false,
        "modifier": false
      },
      "images": [
        {
          "scale": 1,
          "url": "https://emotes.example.com/openemote/v1/assets/hype_01_1x.webp",
          "width": 32,
          "height": 32,
          "mime": "image/webp"
        },
        {
          "scale": 2,
          "url": "https://emotes.example.com/openemote/v1/assets/hype_01_2x.webp",
          "width": 64,
          "height": 64,
          "mime": "image/webp"
        }
      ]
    }
  ]
}
```

## 3) Add assets

- Place emote files in `assets/`.
- Keep file names stable and version with content hash when possible.
- Prefer `webp` for size/perf.

## 4) Serve locally (quick test)

```bash
python3 -m http.server 8080
```

Manifest URL for testing:
- `http://localhost:8080/manifest.json`

## 5) Publish to HTTPS host

Host files under:
- `https://<your-domain>/openemote/v1/manifest.json`
- `https://<your-domain>/openemote/v1/assets/...`

## 6) Add provider in Chatterino OpenEmote

In `Emotes -> Providers`:
- Add provider
- Type: `openemote`
- Base URL: `https://<your-domain>/openemote/v1`
- Enable provider
- Set priority relative to BTTV/7TV/FFZ

## 7) Optional auth

If you want private packs:
- Put a reverse proxy in front (basic auth, token auth, or signed URL).
- Use read-only tokens.
- Keep token in client keychain.

## 8) Safe defaults

- Keep HTTPS enabled.
- Reject oversized uploads in your upload pipeline.
- Keep immutable file names for assets (cache-friendly).
- Increment `version` and update `updated_at` on every publish.

## 9) Troubleshooting

- Emote not showing:
- Check manifest URL reachable.
- Check image URL reachable.
- Check provider enabled and priority.
- Check name collisions with higher-priority providers.

- Pack loads but stale:
- Bump manifest `version`.
- Change `updated_at`.
- Invalidate CDN cache for `manifest.json`.

## 10) Zero-cost hosting options

- GitHub Pages / Cloudflare Pages / Netlify static deploy.
- Object storage + CDN (Cloudflare R2, S3-compatible buckets).

The self-hosted path is intentionally minimal: static files + HTTPS.

