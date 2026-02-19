# OpenEmote Contributor Notes (Upstream-Aligned)

This fork follows Chatterino upstream contribution expectations to keep mergeability high.

## Keep in sync with upstream standards

1. Create a dedicated branch per change. Do not work directly on `master`.
2. Run `clang-format` on touched C++ files before opening a PR.
3. Prefer additive, backward-compatible behavior changes.
4. Follow existing C++/Qt idioms in the codebase (naming, ownership, const-correctness).
5. Keep user-facing command usage strings consistent (`Usage: ...`, `<required>`, `[optional]`).

## OpenEmote-specific maintainer guardrails

1. No forced account/signup requirements for baseline user flows.
2. Keep self-host surfaces optional and out of the default non-streamer path.
3. Preserve performance and stability as first-class constraints for high-volume moderation usage.
4. Keep defaults reversible and avoid migrations that break stock Chatterino compatibility.

## Ownership and merge policy

1. Maintainer owner for this fork: `@orbinyan` (CODEOWNERS gate).
2. PRs can come from anyone; merge requires review and green CI.
3. Merge baseline: affected builds pass and relevant tests pass.
4. Do not rewrite history to remove contributor attribution.
5. Keep prior contributors credited via git history and PR metadata.
6. Enable GitHub branch protection on `master`:
   - require pull request reviews
   - require status checks to pass before merge
   - include Code Owners in required review path

## Reference

- Upstream source of truth:
  - `CONTRIBUTING.md` in this repository
  - https://wiki.chatterino.com/Contributing%20for%20Developers/
