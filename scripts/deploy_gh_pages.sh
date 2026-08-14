#!/usr/bin/env bash
# Deploy a built dist-web to a branch for GitHub Pages.
#
# Usage:
#   ./scripts/deploy_gh_pages.sh [dist-dir] [remote] [branch] [subdir]
#     dist-dir  build output (default: app/guiproc/dist-web)
#     remote    git remote to push (default: origin)
#     branch    pages branch (default: gh-pages)
#     subdir    put the files under this subdir in the branch (default: '' = branch
#               root). Use e.g. "demo" so the site appears at /<repo>/demo/.
#
# Then enable Pages in the GitHub web UI:
#   Repo → Settings → Pages → Build and deployment → Source: Deploy from a
#   branch → <branch> → / (root)
#
# No build happens here — run scripts/build_generic_web.sh first. The static
# files are copied onto a worktree of the branch and force-pushed, so large
# index.data never lands in your main branch history. Idempotent: unchanged
# files produce no new commit.
set -euo pipefail

cd "$(dirname "$0")/.."
DIST="${1:-app/guiproc/dist-web}"
REMOTE="${2:-origin}"
BRANCH="${3:-gh-pages}"
SUBDIR="${4:-}"

[ -f "$DIST/index.html" ] || { echo "no index.html in $DIST — build first (see README §8)" >&2; exit 1; }
DIST="$(cd "$DIST" && pwd)"

echo "==> deploying $DIST → $REMOTE/$BRANCH${SUBDIR:+/$SUBDIR}"
TMP="$(mktemp -d)"
trap 'git worktree remove --force "$TMP" 2>/dev/null || true; rm -rf "$TMP"' EXIT

git fetch "$REMOTE" "$BRANCH" 2>/dev/null || true
if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
    git worktree add "$TMP" "$BRANCH"
elif git show-ref --verify --quiet "refs/remotes/$REMOTE/$BRANCH"; then
    git worktree add "$TMP" -b "$BRANCH" "refs/remotes/$REMOTE/$BRANCH"
else
    git worktree add "$TMP" -b "$BRANCH"
fi

# Replace everything except .git, then lay files into (subdir/) on the branch.
find "$TMP" -mindepth 1 -maxdepth 1 -not -name .git -exec rm -rf {} +
: > "$TMP/.nojekyll"   # keep GitHub Pages from running Jekyll over the assets
DEST="$TMP${SUBDIR:+/$SUBDIR}"
mkdir -p "$DEST"
for f in index.html index.js index.wasm index.data coi-serviceworker.js; do
    [ -f "$DIST/$f" ] && cp "$DIST/$f" "$DEST/"
done

git -C "$TMP" add -A
if git -C "$TMP" diff --cached --quiet; then
    echo "==> nothing changed since last deploy"
else
    git -C "$TMP" -c user.name="gh-pages-deploy" -c user.email="deploy@localhost" \
        commit -m "Deploy web build $(date +%Y%m%d-%H%M%S)"
fi
git -C "$TMP" push --force "$REMOTE" "$BRANCH"

echo "==> done. Enable Pages in the web UI:"
echo "    Settings → Pages → Deploy from a branch → $BRANCH → / (root)"
echo "    URL: https://<owner>.github.io/<repo>/${SUBDIR:+$SUBDIR/}"
