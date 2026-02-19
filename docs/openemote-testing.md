# OpenEmote Testing Runbook

## Linux smoke path

From repo root:

```bash
chmod +x scripts/openemote_smoke_linux.sh scripts/openemote_mock_server.py
./scripts/openemote_smoke_linux.sh
```

Then launch:

```bash
nix --extra-experimental-features 'nix-command flakes' develop -c ./build/release/bin/chatterino
```

## In-app configuration

In `Settings -> General`:

- Enable custom provider global emotes
- Enable custom provider channel emotes
- Set custom provider API base URLs to `http://127.0.0.1:18080`
- Set `Whispers emote context channel` to an active Twitch channel name

## Emote behavior checks

Send the following in appropriate contexts:

- `OPENHYPE` in a normal channel (global mock emote)
- `OPENWAVE` in a Twitch channel tab (channel mock emote)
- `OPENWAVE` in a whisper (with whisper context configured)

Expected:

- All three render as emotes (not plain text)

## Credential safety checks

Default policy:

- Keychain path is preferred
- Insecure plaintext fallback is disabled by default

Verify:

1. Keep `Allow insecure plaintext credential storage (not recommended)` OFF.
2. Disable keychain integration (Linux), restart, and attempt credential persistence.
3. Confirm warning indicates plaintext fallback is blocked by default.

Opt-in fallback path:

1. Turn `Allow insecure plaintext credential storage` ON.
2. Repeat persistence action.
3. Confirm warning indicates plaintext storage is enabled.

## CI/release follow-up

- CI workflow: `.github/workflows/openemote-ci.yml`
- Tag release workflow: `.github/workflows/openemote-release.yml`
- Release process doc: `docs/openemote-release.md`
