"""The gh-pages tree of the AXIS Micro BBS site: the site's own files, releases/ and builds/.

    python -I tools/ghpages.py site --site site --installer installer [--into DIR]
    python -I tools/ghpages.py release --tag v0.60 --from DIR [--into DIR]

--into defaults to $GHP, which tools/ci/ghpages_publish.sh sets to a fresh copy of gh-pages.

The site workflow mirrors site/ (and the installer lists) and never touches releases/ or
builds/. The release workflow adds releases/<tag>/ and rewrites releases/index.json. Only the
three newest releases (by version number) are kept; custom builds are dropped oldest first
(the "built" time in builds/<fingerprint>/manifest.json, UTC ISO 8601; a build without one
counts as oldest) once builds/ passes 300 MB. A dropped build simply builds again on request.
"""
import argparse
import glob
import json
import os
import re
import shutil
import sys

KEEP = {"releases", "builds", ".git", ".nojekyll"}
KEEP_RELEASES = 3
BUILDS_CAP = 300 * 1024 * 1024
TAG = re.compile(r"^v[0-9][0-9A-Za-z._-]*$")


def _remove(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    else:
        os.remove(path)


def merge(site_dir, gh_pages_dir, installer_dir=None):
    """Make gh_pages_dir hold exactly the site's files, keeping releases/ and builds/."""
    clash = KEEP.intersection(os.listdir(site_dir)) - {".nojekyll"}
    if clash:
        raise ValueError("the site may not have its own %s" % ", ".join(sorted(clash)))
    os.makedirs(gh_pages_dir, exist_ok=True)
    for name in os.listdir(gh_pages_dir):
        if name not in KEEP:
            _remove(os.path.join(gh_pages_dir, name))
    for name in os.listdir(site_dir):
        src = os.path.join(site_dir, name)
        dst = os.path.join(gh_pages_dir, name)
        if os.path.isdir(src):
            shutil.copytree(src, dst)
        else:
            shutil.copyfile(src, dst)
    if installer_dir:
        out = os.path.join(gh_pages_dir, "installer")
        os.makedirs(out, exist_ok=True)
        for path in sorted(glob.glob(os.path.join(installer_dir, "*.json"))):
            shutil.copyfile(path, os.path.join(out, os.path.basename(path)))
    # Branch deploys run Jekyll unless this file is there.
    open(os.path.join(gh_pages_dir, ".nojekyll"), "w").close()


def version_key(tag):
    """v0.9 < v0.52 < v0.100: numbers compare as numbers."""
    return [(0, int(p), "") if p.isdigit() else (1, 0, p) for p in re.split(r"(\d+)", tag) if p]


def releases(gh_pages_dir):
    """Release tags on the branch, newest first."""
    root = os.path.join(gh_pages_dir, "releases")
    if not os.path.isdir(root):
        return []
    tags = [n for n in os.listdir(root) if os.path.isdir(os.path.join(root, n))]
    return sorted(tags, key=version_key, reverse=True)


def write_index(gh_pages_dir):
    tags = releases(gh_pages_dir)
    idx = {"latest": tags[0] if tags else None,
           "previous": tags[1] if len(tags) > 1 else None,
           "releases": tags}
    root = os.path.join(gh_pages_dir, "releases")
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, "index.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(idx, fh, indent=1)
        fh.write("\n")
    return idx


def _dir_size(path):
    total = 0
    for dirpath, _dirs, files in os.walk(path):
        for f in files:
            total += os.path.getsize(os.path.join(dirpath, f))
    return total


def _built(build_dir):
    try:
        with open(os.path.join(build_dir, "manifest.json"), encoding="utf-8") as fh:
            value = json.load(fh).get("built")
        return value if isinstance(value, str) else ""
    except (OSError, ValueError, AttributeError):
        return ""


def prune(gh_pages_dir, keep_releases=KEEP_RELEASES, builds_cap=BUILDS_CAP):
    """Drop releases past the newest keep_releases and the oldest builds past builds_cap.
    Rewrites releases/index.json. Returns the removed folders (relative paths)."""
    removed = []
    for tag in releases(gh_pages_dir)[keep_releases:]:
        shutil.rmtree(os.path.join(gh_pages_dir, "releases", tag))
        removed.append("releases/" + tag)
    root = os.path.join(gh_pages_dir, "builds")
    if os.path.isdir(root):
        builds = []
        for name in os.listdir(root):
            path = os.path.join(root, name)
            if os.path.isdir(path):
                builds.append((_built(path), name, _dir_size(path)))
        total = sum(b[2] for b in builds)
        for _when, name, size in sorted(builds):
            if total <= builds_cap:
                break
            shutil.rmtree(os.path.join(root, name))
            removed.append("builds/" + name)
            total -= size
    write_index(gh_pages_dir)
    return removed


def add_release(gh_pages_dir, tag, src_dir):
    """Put src_dir at releases/<tag>/ (replacing an earlier copy), prune, rewrite the index."""
    if not isinstance(tag, str) or not TAG.match(tag) or ".." in tag:
        raise ValueError("not a release tag: %r" % (tag,))
    dst = os.path.join(gh_pages_dir, "releases", tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(src_dir, dst)
    return prune(gh_pages_dir)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="ghpages")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("site")
    s.add_argument("--site", required=True)
    s.add_argument("--installer")
    r = sub.add_parser("release")
    r.add_argument("--tag", required=True)
    r.add_argument("--from", dest="src", required=True)
    for p in (s, r):
        p.add_argument("--into", default=os.environ.get("GHP", ""))
    args = ap.parse_args(argv)
    if not args.into:
        print("ghpages: no target folder (--into or GHP)", file=sys.stderr)
        return 1
    try:
        if args.cmd == "site":
            merge(args.site, args.into, installer_dir=args.installer)
            print("site copied; releases: %s" % (", ".join(releases(args.into)) or "none"))
        else:
            gone = add_release(args.into, args.tag, args.src)
            idx = write_index(args.into)
            print("release %s added; latest %s, previous %s" % (args.tag, idx["latest"], idx["previous"]))
            for path in gone:
                print("removed " + path)
    except (OSError, ValueError) as e:
        print("ghpages: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
