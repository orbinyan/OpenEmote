# OpenEmote Release Cadence

This project aims to become the reliable open-source maintainer path by shipping fast and predictably.

## Cadence

1. Patch releases: weekly or faster for regressions/security issues.
2. Minor releases: every 2 to 4 weeks for additive features.
3. Major releases: only when compatibility boundaries change.

## Service levels (maintainer targets)

1. Triage new issues within 72 hours.
2. Repro + owner assignment on valid bugs within 7 days.
3. Hotfix branch for severe breakage as soon as practical.

## Change policy

1. Prefer additive, backward-compatible changes.
2. Feature flags for risky integration work.
3. Clear migration notes for behavioral changes.
4. Public changelog for every release.

## Stability expectations

1. Keep side-panel efficiency and rendering performance intact.
2. Do not degrade baseline chat usability for new surfaces.
3. Treat moderation/reporting path failures as high-priority defects.

## Definition of done for releases

1. Documented user-visible changes.
2. Updated protocol/schema docs when relevant.
3. Rollback path identified for risky changes.
