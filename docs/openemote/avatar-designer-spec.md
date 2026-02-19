# OpenEmote Avatar Designer Spec (v1)

## Goal
Define a stable, self-host friendly avatar model format that is separate from usernames, emotes, and badges.

## Product behavior
- Username text remains visible in chat.
- Avatar model is a separate profile asset shown on usercard.
- Badges remain separate identity markers.
- Interaction commands (`!shake`, `!hug`, `!wave`) are optional and safe-fallback.

## Runtime contract (client)
- Parse optional metadata tags:
  - `openemote-avatar-model`
  - `openemote-avatar-skin`
  - `openemote-avatar-idle`
  - `openemote-avatar-action`
  - `openemote-avatar-target`
- If action tag is absent, parse action from message text:
  - `!shake @user`
  - `!hug @user`
  - `!wave @user`
- Unsupported/missing assets must not crash rendering.
- Fallback: static profile avatar + text summary.

## Designer output contract (website)
- Must emit JSON conforming to `docs/openemote/avatar-model.schema.json`.
- Required fields:
  - `version`
  - `model_id`
  - `rig`
  - `base_size`
  - `skin`
- Optional:
  - `idle`
  - `interactions.shake|hug|wave`

## Moderation + safety expectations
- Reject impersonation and forbidden content.
- SFW by default; no explicit content.
- Keep an audit trail of moderation decisions per model revision.

## Self-host compatibility
- Server operator chooses hosting and moderation policy.
- Client only enforces format/safety fallback, not hosting policy.
