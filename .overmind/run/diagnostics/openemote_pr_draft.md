# PR Draft: OpenEmote integration foundation + secure group whisper

## Title
feat(openemote): add secure group whisper and integration foundation

## Summary
This PR introduces the OpenEmote integration surface in Chatterino with secure group whisper support, import/integration utilities, API parsing and test coverage, plus supporting docs and CLI tooling.

## What's included

### OpenEmote modules and APIs
- Adds OpenEmote API client:
  - `src/providers/openemote/OpenEmoteApiClient.hpp`
  - `src/providers/openemote/OpenEmoteApiClient.cpp`
- Adds OpenEmote utility modules:
  - `src/util/OpenEmoteImport.{hpp,cpp}`
  - `src/util/OpenEmoteIntegration.{hpp,cpp}`
  - `src/util/OpenEmoteSecureGroupWhisper.hpp`

### Integration wiring
- Application bootstrap and command path wiring for OpenEmote behavior:
  - `src/Application.cpp`
  - `src/CMakeLists.txt`
  - `src/common/Args.cpp`
  - `src/common/Args.hpp`
  - `src/controllers/commands/CommandController.cpp`
  - `src/controllers/commands/builtin/twitch/SendWhisper.*`
  - `src/main.cpp`
  - `src/messages/Message.hpp`
  - `src/messages/MessageBuilder.cpp`
  - `src/providers/twitch/IrcMessageHandler.cpp`
  - `src/providers/seventv/SeventvEventAPI.hpp`
  - `src/singletons/ImageUploader.cpp`
  - `src/singletons/Settings.hpp`
  - `src/widgets/dialogs/*`
  - `src/widgets/helper/ChannelView.cpp`
  - `src/widgets/settingspages/*`
  - `src/widgets/splits/SplitInput.cpp`
- Test registration updates:
  - `tests/CMakeLists.txt`
  - `tests/src/MessageLayoutContainer.cpp`

### Tests and assets
- Adds tests:
  - `tests/src/OpenEmoteApiClient.cpp`
  - `tests/src/OpenEmoteImport.cpp`
  - `tests/src/OpenEmoteSecureGroupWhisper.cpp`
  - `tests/src/WidgetHelpers.cpp`
- Includes secure whisper regression for `QStringView`-safe parsing API:
  - `decodeEnvelope(..., QStringView{QStringLiteral(\"secret\")}, ...)`

### Docs and dev tooling
- Added OpenEmote governance and release docs:
  - `docs/openemote/CONTRIBUTING_NOTES.md`
  - `docs/openemote/OPEN_CORE_POLICY.md`
  - `docs/openemote/RELEASE_CADENCE.md`
  - `docs/openemote/RELEASE_DISTRIBUTION.md`
  - `docs/openemote/avatar-designer-spec.md`
  - `docs/openemote/avatar-model.schema.json`
  - `docs/openemote/integration-config.example.json`
  - `docs/openemote/integration-pack.schema.json`
  - `README.md` (OpenEmote fork docs + artifact notes)
- Added fork ownership and contributor policy:
  - `.github/CODEOWNERS`
- Added developer helper:
  - `scripts/openemote-dev.sh`

## Validation
- `ctest -R OpenEmote --output-on-failure` passed `34/34`
- `ctest -R OpenEmoteSecureGroupWhisper --output-on-failure` passed `3/3`
- Patch artifact available:
  - `.overmind/run/diagnostics/openemote-commit-2fc64d9.patch`

## Commit
- `2fc64d9`

## Apply this cleanly
- `git cherry-pick 2fc64d9`
- or
- `git apply .overmind/run/diagnostics/openemote-commit-2fc64d9.patch`

