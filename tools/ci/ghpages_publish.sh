#!/bin/sh
# Change the gh-pages branch with one command and push it.
#   sh tools/ci/ghpages_publish.sh [--allow-prune] "commit message" command [args...]
# Run from a checkout whose "origin" can be pushed to (actions/checkout leaves the token for it).
# The command runs with GHP set to a fresh copy of the current gh-pages tree (empty when the
# branch does not exist yet) and edits it in place. The result is pushed as a single commit
# with no parent, so pruned images do not pile up in the branch history. When someone else
# pushed in between, the lease fails and the whole thing runs again on their tree.
# With no history to go back to, a tree that has lost all of releases/ or builds/ is refused
# unless --allow-prune is given (only the release path, which prunes, passes it).
set -eu
allow_prune=0
if [ "${1:-}" = "--allow-prune" ]; then
    allow_prune=1
    shift
fi
msg=${1:?commit message}
shift
[ "$#" -gt 0 ] || { echo "ghpages_publish: no command" >&2; exit 2; }
base=${GHP_TMP:-${RUNNER_TEMP:-/tmp}/ghpages}
gitdir=$(git rev-parse --absolute-git-dir)
tries=5
n=1
export GIT_AUTHOR_NAME="${GIT_AUTHOR_NAME:-github-actions[bot]}"
export GIT_AUTHOR_EMAIL="${GIT_AUTHOR_EMAIL:-41898282+github-actions[bot]@users.noreply.github.com}"
export GIT_COMMITTER_NAME="${GIT_COMMITTER_NAME:-$GIT_AUTHOR_NAME}"
export GIT_COMMITTER_EMAIL="${GIT_COMMITTER_EMAIL:-$GIT_AUTHOR_EMAIL}"
while :; do
    rm -rf "$base"
    mkdir -p "$base/tree"
    GHP=$base/tree
    export GHP
    old=
    # ls-remote exits 2 when the branch is not there; any other failure stops here.
    have=0
    git ls-remote --exit-code --heads origin gh-pages > /dev/null || have=$?
    if [ "$have" -eq 0 ]; then
        git fetch -q --no-tags --depth=1 origin "+refs/heads/gh-pages:refs/ghpages/old"
        old=$(git rev-parse refs/ghpages/old)
        git archive "$old" | (cd "$GHP" && tar -xf -)
    elif [ "$have" -eq 2 ]; then
        echo "no gh-pages branch yet, starting one"
    else
        echo "ghpages_publish: cannot reach origin" >&2
        exit 1
    fi
    "$@"
    if [ ! -f "$GHP/index.html" ]; then
        echo "ghpages_publish: the tree has no index.html (run the Pages workflow first); nothing pushed" >&2
        exit 1
    fi
    GIT_INDEX_FILE=$base/index
    export GIT_INDEX_FILE
    git -C "$GHP" --git-dir="$gitdir" --work-tree="$GHP" add -A -f .
    tree=$(git write-tree)
    unset GIT_INDEX_FILE
    if [ -n "$old" ] && [ "$allow_prune" -eq 0 ]; then
        for d in releases builds; do
            if [ -n "$(git ls-tree "$old" -- "$d")" ] && [ -z "$(git ls-tree "$tree" -- "$d")" ]; then
                echo "ghpages_publish: this change removes all of $d/ from gh-pages; nothing pushed" >&2
                exit 1
            fi
        done
    fi
    if [ -n "$old" ] && [ "$tree" = "$(git rev-parse "$old^{tree}")" ]; then
        echo "nothing changed on gh-pages"
        exit 0
    fi
    commit=$(git commit-tree "$tree" -m "$msg")
    if [ -n "$old" ]; then
        lease="--force-with-lease=refs/heads/gh-pages:$old"
    else
        lease="--force-with-lease=refs/heads/gh-pages:"
    fi
    if git push -q "$lease" origin "$commit:refs/heads/gh-pages"; then
        echo "gh-pages is now $commit"
        exit 0
    fi
    if [ "$n" -ge "$tries" ]; then
        echo "ghpages_publish: push refused $tries times" >&2
        exit 1
    fi
    n=$((n + 1))
    echo "gh-pages moved, trying again ($n of $tries)"
    sleep "${GHP_RETRY_SLEEP:-10}"
done
