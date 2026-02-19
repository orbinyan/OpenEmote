# OpenEmote: SevenTV Merge-Down Strategy

This repository stays anchored to `chatterino2` history.
We import selected `SevenTV/chatterino7` work as explicit integration slices.

## Policy

- Canonical base: `Chatterino/chatterino2`
- Import source: `seventv-source/chatterino7`
- No base switch to `chatterino7`
- Every imported commit must preserve attribution via `git cherry-pick -x`
- Imports are fail-closed: no silent fallback behavior masking regressions

## Remotes

- `origin`: current repo remote (currently `chatterino2` upstream URL)
- `seventv-source`: `https://github.com/SevenTV/chatterino7`

## Branching model

- Product branch: `main` (OpenEmote)
- Integration branch per slice: `integration/seventv-sXX-<name>`

## Slice process

1. Create branch from current product tip.
2. Import a narrow set of SevenTV commits (`cherry-pick -x`) or file-level patches.
3. Resolve conflicts in favor of OpenEmote architecture:
4. Keep open custom-provider path generic (`/emotes/custom/*`), not platform-locked.
5. Preserve current OpenEmote UX additions:
6. Modifier aliases (`!flr`, `!fud`, tint aliases)
7. Frequent emote phrase storage/tab/completion
8. Add migration note if settings schema changed.
9. Record evidence in `docs/openemote/seventv-slices/<slice>.md`.

## Initial slice ledger

- `S01`: Open custom provider path on chatterino2 base (implemented)
- `S02`: Custom modifiers + frequent tab/completion (implemented)
- `S10`: Candidate SevenTV merge-down set (pending curation)
- 7TV personal emote lifecycle
- Paint rendering internals worth upstream parity
- Event/update robustness patches with low blast radius

## Guardrails

- Do not import paywalled/product-policy behavior.
- Keep user-facing settings explicit and opt-in.
- Keep performance-sensitive paths allocation-light:
- parse-time: alias map cache
- render-time: no network, bounded work per emote layer

## Helper script

Use `scripts/seventv_merge_down.sh` to create a per-slice integration branch, cherry-pick curated commits with attribution, and emit a receipt.

Example:

```bash
scripts/seventv_merge_down.sh   --slice S10   --name emote-lifecycle   --base main   --source seventv-source/chatterino7   --commits-file docs/openemote/seventv-slices/S10.commits
```
