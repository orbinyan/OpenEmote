#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="$(basename "$0")"

usage() {
  cat <<USAGE
Usage:
  $SCRIPT_NAME \
    --slice S10 \
    --name emote-lifecycle \
    --base main \
    --source seventv-source/chatterino7 \
    --commits <sha1,sha2,...>

  $SCRIPT_NAME \
    --slice S10 \
    --name emote-lifecycle \
    --base main \
    --source seventv-source/chatterino7 \
    --commits-file docs/openemote/seventv-slices/S10.commits

Required:
  --slice         Slice id (e.g. S10)
  --name          Branch suffix for this candidate
  --base          Local base branch (default: main)
  --source        Source ref to merge down from (default: seventv-source/chatterino7)

One of:
  --commits       Comma-separated commit list
  --commits-file  Newline-separated commit list file

Optional:
  --receipt-dir   Receipt directory (default: docs/openemote/seventv-slices)
  --no-checkout   Do not switch to created branch
USAGE
}

SLICE=""
NAME=""
BASE="main"
SOURCE_REF="seventv-source/chatterino7"
COMMITS_CSV=""
COMMITS_FILE=""
RECEIPT_DIR="docs/openemote/seventv-slices"
DO_CHECKOUT=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --slice)
      SLICE="${2:-}"
      shift 2
      ;;
    --name)
      NAME="${2:-}"
      shift 2
      ;;
    --base)
      BASE="${2:-}"
      shift 2
      ;;
    --source)
      SOURCE_REF="${2:-}"
      shift 2
      ;;
    --commits)
      COMMITS_CSV="${2:-}"
      shift 2
      ;;
    --commits-file)
      COMMITS_FILE="${2:-}"
      shift 2
      ;;
    --receipt-dir)
      RECEIPT_DIR="${2:-}"
      shift 2
      ;;
    --no-checkout)
      DO_CHECKOUT=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "$SLICE" || -z "$NAME" ]]; then
  echo "--slice and --name are required" >&2
  usage
  exit 2
fi

if [[ -n "$COMMITS_CSV" && -n "$COMMITS_FILE" ]]; then
  echo "Use only one of --commits or --commits-file" >&2
  exit 2
fi

if [[ -z "$COMMITS_CSV" && -z "$COMMITS_FILE" ]]; then
  echo "Provide --commits or --commits-file" >&2
  exit 2
fi

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Not inside a git work tree" >&2
  exit 2
fi

if ! git show-ref --verify --quiet "refs/heads/$BASE"; then
  echo "Base branch '$BASE' does not exist locally" >&2
  exit 2
fi

if ! git rev-parse --verify "$SOURCE_REF^{commit}" >/dev/null 2>&1; then
  echo "Source ref '$SOURCE_REF' was not found" >&2
  echo "Run: git fetch seventv-source chatterino7" >&2
  exit 2
fi

if [[ -n "$COMMITS_FILE" && ! -f "$COMMITS_FILE" ]]; then
  echo "Commits file not found: $COMMITS_FILE" >&2
  exit 2
fi

if [[ -n "$COMMITS_CSV" ]]; then
  mapfile -t COMMITS < <(printf '%s' "$COMMITS_CSV" | tr ',' '\n' | sed '/^\s*$/d')
else
  mapfile -t COMMITS < <(sed '/^\s*#/d;/^\s*$/d' "$COMMITS_FILE")
fi

if [[ ${#COMMITS[@]} -eq 0 ]]; then
  echo "No commits to import" >&2
  exit 2
fi

for commit in "${COMMITS[@]}"; do
  if ! git rev-parse --verify "$commit^{commit}" >/dev/null 2>&1; then
    echo "Commit not found: $commit" >&2
    exit 2
  fi
  if ! git merge-base --is-ancestor "$commit" "$SOURCE_REF"; then
    echo "Commit is not reachable from source ref '$SOURCE_REF': $commit" >&2
    exit 2
  fi
done

slugify() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/-/g;s/-\{2,\}/-/g;s/^-//;s/-$//'
}

SLICE_LOWER="$(slugify "$SLICE")"
NAME_LOWER="$(slugify "$NAME")"
BRANCH="integration/seventv-${SLICE_LOWER}-${NAME_LOWER}"

if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
  echo "Branch already exists: $BRANCH" >&2
  exit 2
fi

if [[ "$DO_CHECKOUT" -eq 1 ]]; then
  git switch "$BASE"
  git switch -c "$BRANCH"
else
  git branch "$BRANCH" "$BASE"
fi

for commit in "${COMMITS[@]}"; do
  echo "Cherry-picking $commit"
  git cherry-pick -x "$commit"
done

mkdir -p "$RECEIPT_DIR"
RECEIPT_FILE="$RECEIPT_DIR/${SLICE}.md"

{
  echo "# ${SLICE} merge-down receipt"
  echo
  echo "- Date (UTC): $(date -u +'%Y-%m-%dT%H:%M:%SZ')"
  echo "- Source ref: \\`$SOURCE_REF\\`"
  echo "- Base branch: \\`$BASE\\`"
  echo "- Integration branch: \\`$BRANCH\\`"
  echo "- Imported commits:"
  for commit in "${COMMITS[@]}"; do
    short="$(git rev-parse --short "$commit")"
    subject="$(git show -s --format='%s' "$commit")"
    echo "  - \\`$short\\` $subject"
  done
  echo
  echo "## Conflict notes"
  echo
  echo "- Fill in any manual conflict resolutions."
  echo
  echo "## OpenEmote guardrails checklist"
  echo
  echo "- [ ] Custom provider path remains open/generic."
  echo "- [ ] Modifier alias path remains user-configurable."
  echo "- [ ] Frequent tab/completion unchanged or migrated."
  echo "- [ ] No paywalled behavior imported."
  echo "- [ ] Performance impact reviewed in hot paths."
} > "$RECEIPT_FILE"

echo "Created branch: $BRANCH"
echo "Wrote receipt: $RECEIPT_FILE"
