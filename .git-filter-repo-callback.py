# Rewrites author/committer identity for any commits that leak minuo identity.
# Keep other contributors untouched.

def _needs_scrub(name: bytes, email: bytes) -> bool:
    nl = (name or b'').lower()
    el = (email or b'').lower()
    return (
        b'minuo' in nl
        or b'minuo' in el
        or b'jack-compute-host' in nl
        or el.endswith(b'@minuo.ai')
    )

NEW_NAME = b'orbinyan'
NEW_EMAIL = b'17157590+orbinyan@users.noreply.github.com'


def commit_callback(commit):
    if _needs_scrub(commit.author_name, commit.author_email):
        commit.author_name = NEW_NAME
        commit.author_email = NEW_EMAIL
    if _needs_scrub(commit.committer_name, commit.committer_email):
        commit.committer_name = NEW_NAME
        commit.committer_email = NEW_EMAIL

    # Also scrub obvious identity strings in commit message text.
    if commit.message:
        commit.message = (
            commit.message
            .replace(b'jack-minuo', b'orbinyan')
            .replace(b'Jack-minuo', b'orbinyan')
            .replace(b'JACK-MINUO', b'orbinyan')
            .replace(b'@minuo.ai', b'@users.noreply.github.com')
        )
