"""Custom AXIS Micro BBS builds asked for with the "Build request" issue form.

    python -I tools/build_request.py check --event EVENT.json --index INDEX.json --builds LIST --out FILE
    python -I tools/build_request.py manifest BUILD_DIR OUT_DIR --off a,b --release v0.60 --fingerprint FP
    python -I tools/build_request.py publish --from DIR --fingerprint FP [--into DIR]
    python -I tools/build_request.py answer --status S ... --body FILE

The issue body is the only untrusted text. It is read here, from the event file, and nothing
of it is passed on: only feature ids from installer/features.json, a release tag taken from
releases/index.json, the fingerprint and the board id reach the build. Any line the form does
not make is refused, and a refusal never quotes the body.

check writes GitHub step outputs (status = build, exists, preset or refused; reason, off,
target, release, fingerprint). An existing builds/<fingerprint>/ is answered with its link at
once; otherwise a user may make at most 3 build requests a day, counted from their issues with
the build-request or build-answered label opened in the last 24 hours (the issues API,
GITHUB_TOKEN; answering an issue swaps build-request for build-answered). The issue is
read again through the API first: a closed or unlabelled issue is skipped, and an issue the bot
has already answered with a link gets no second build (status skip or done).
Standard library only; runs on Windows and Linux.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import sys
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import buildcost  # noqa: E402
import buildopts  # noqa: E402
import ghpages  # noqa: E402

LABEL = "build-request"
ANSWERED_LABEL = "build-answered"  # answer swaps LABEL for this; both count toward the cap
COUNTED = {LABEL, ANSWERED_LABEL}
PER_PAGE = 100
MAX_PAGES = 10
OFF_HEADING = "Leave these features out"
BOARD_HEADING = "Board"
TARGETS = {"cyd": "CYD (the board with the screen)", "esp32": "Bare ESP32 (no screen, headless)"}
DAILY_CAP = 3
BOT = "github-actions[bot]"
ANSWERED = "install.html"  # every closing answer links the installer; one answer per issue
FINGERPRINT = re.compile(r"^[0-9a-f]{12}$")
REPO = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
STAMP = "%Y-%m-%dT%H:%M:%SZ"
BOX = re.compile(r"^- \[([ xX])\] (.*)$")
# The images a build may hold, by role: (file names, offset). Anything else is refused.
ROLES = {"bootloader": (("bootloader.bin",), buildcost.BOOTLOADER_OFFSET),
         "partitions": (("partitions.bin",), buildcost.PARTITIONS_OFFSET),
         "otadata": (("ota_data_initial.bin", "boot_app0.bin"), None),
         "app": (("firmware.bin",), None)}


def box_label(feature):
    """The checkbox text for a feature, as the form shows it and the body repeats it."""
    return "%s (%s)" % (feature["label"], feature["id"])


def _selectable(features):
    return [f for f in features if not f.get("reserved")]


def parse(issue_body, features=None):
    """(off, target) from the form's lines. Raises ValueError for anything else."""
    if features is None:
        features = buildopts.load(ROOT)[0]
    boxes = {box_label(f): f["id"] for f in _selectable(features)}
    boards = {v: k for k, v in TARGETS.items()}
    if not isinstance(issue_body, str) or not issue_body.strip():
        raise ValueError("the issue has no build request form in it")
    section = None
    seen = set()
    ticked = set()
    listed = set()
    target = None
    for n, raw in enumerate(issue_body.replace("\r\n", "\n").replace("\r", "\n").split("\n"), 1):
        line = raw.rstrip()
        if not line:
            continue
        if line.startswith("### "):
            heading = line[4:]
            if heading not in (OFF_HEADING, BOARD_HEADING) or heading in seen:
                raise ValueError("line %d is not part of the build request form" % n)
            seen.add(heading)
            section = heading
            continue
        if section == OFF_HEADING:
            m = BOX.match(line)
            if not m or m.group(2) not in boxes or m.group(2) in listed:
                raise ValueError("line %d is not one of the form's feature boxes" % n)
            listed.add(m.group(2))
            if m.group(1) != " ":
                ticked.add(boxes[m.group(2)])
            continue
        if section == BOARD_HEADING and line in boards and target is None:
            target = boards[line]
            continue
        raise ValueError("line %d is not part of the build request form" % n)
    if OFF_HEADING not in seen:
        raise ValueError("the request has no \"%s\" list" % OFF_HEADING)
    if target is None:
        raise ValueError("the request names no board")
    return ticked, target


def _time(text):
    return datetime.datetime.strptime(text, STAMP).replace(tzinfo=datetime.timezone.utc)


def _now(now=None):
    if now:
        return _time(now)
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)


def rate_ok(user, today_requests):
    """True while user has at most DAILY_CAP requests in today_requests (this one included)."""
    mine = [r for r in today_requests
            if "pull_request" not in r and str(r.get("user", {}).get("login", "")).lower() == user.lower()]
    return len(mine) <= DAILY_CAP


def recent(requests, now=None, upto=None):
    """The requests opened in the 24 hours before now, up to issue number upto."""
    start = _now(now) - datetime.timedelta(hours=24)
    out = []
    for r in requests:
        try:
            if _time(r["created_at"]) < start:
                continue
        except (KeyError, TypeError, ValueError):
            continue
        if upto is not None and r.get("number", 0) > upto:
            continue
        out.append(r)
    return out


def _label_names(item):
    labels = item.get("labels") if isinstance(item, dict) else None
    return {str(label.get("name", "")) if isinstance(label, dict) else str(label) for label in labels or []}


def fetch_requests(api, repo, user, now=None):
    """The user's issues touched in the last 24 hours that carry build-request or build-answered,
    through api(path). The API ANDs a labels filter, so it asks for all of the user's issues and
    keeps those here. Fails closed on an odd answer or more pages than it reads."""
    since = (_now(now) - datetime.timedelta(hours=24)).strftime(STAMP)
    out = []
    for page in range(1, MAX_PAGES + 1):
        query = urllib.parse.urlencode({"creator": user, "state": "all", "since": since,
                                        "per_page": PER_PAGE, "page": page})
        got = api("/repos/%s/issues?%s" % (repo, query))
        if not isinstance(got, list):
            raise ValueError("the issues API did not answer with a list")
        out += [r for r in got if isinstance(r, dict) and _label_names(r) & COUNTED]
        if len(got) < PER_PAGE:
            return out
    raise ValueError("too many issues to count")


def live_issue(api, repo, number):
    """(issue, comments) as they are now, through api(path). Fails closed on an odd answer."""
    issue = api("/repos/%s/issues/%d" % (repo, number))
    comments = api("/repos/%s/issues/%d/comments?per_page=100" % (repo, number))
    if not isinstance(issue, dict) or not isinstance(comments, list):
        raise ValueError("the issues API did not answer with an issue and its comments")
    return issue, comments


def _answered(comments):
    return any(isinstance(c, dict) and (c.get("user") or {}).get("login") == BOT
               and ANSWERED in str(c.get("body", "")) for c in comments)


def github_api(token, base=None):
    base = (base or os.environ.get("GITHUB_API_URL") or "https://api.github.com").rstrip("/")

    def call(path):
        req = urllib.request.Request(base + path, headers={
            "Accept": "application/vnd.github+json", "Authorization": "Bearer " + token,
            "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "axis-bbs-custom-build"})
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.load(resp)
    return call


def _release(index):
    if not isinstance(index, dict):
        return None
    tag = index.get("latest")
    tags = index.get("releases")
    if isinstance(tag, str) and ghpages.TAG.match(tag) and ".." not in tag and isinstance(tags, list) \
            and tag in tags:
        return tag
    return None


def decide(event, index, builds, fetch, current, now=None, features=None, presets=None):
    """What to do with one issue event. fetch(user) returns the user's recent requests;
    current(number) returns (issue, comments) as they are now.
    Every value returned is safe to write to a step output and a comment."""
    if features is None or presets is None:
        features, presets = buildopts.load(ROOT)
    issue = event.get("issue") or {}
    out = {"status": "refused", "reason": "", "off": "", "target": "", "release": "", "fingerprint": ""}
    live, comments = current(issue.get("number") or 0)
    labels = [str((label or {}).get("name", "")) for label in live.get("labels") or []]
    if live.get("state") != "open" or LABEL not in labels:
        out["status"] = "skip"
        return out
    if _answered(comments):
        out["status"] = "done"
        out["reason"] = "this issue already has its answer; open a new Build request for another combination"
        return out
    try:
        off, target = parse(issue.get("body"), features)
        off = buildopts.resolve(off, features)
    except ValueError as e:
        out["reason"] = str(e)
        return out
    out["off"] = ",".join(sorted(off))
    out["target"] = target
    release = _release(index)
    if release is None:
        out["reason"] = "there is no release on the installer site yet"
        return out
    fp = buildopts.fingerprint(off, release)
    out.update(release=release, fingerprint=fp)
    for p in presets:
        if set(p["off"]) == off:
            out["status"] = "preset"
            out["reason"] = "that is the %s preset, already on the installer page" % p["label"]
            return out
    if "builds/%s/manifest.json" % fp in set(builds):
        out["status"] = "exists"
        return out
    user = str((issue.get("user") or {}).get("login", ""))
    number = issue.get("number") or 0
    reqs = recent(fetch(user), now=now, upto=number)
    if not any(r.get("number") == number for r in reqs):
        reqs.append({"number": number, "user": {"login": user}})
    if not rate_ok(user, reqs):
        out["reason"] = ("at most %d build requests per GitHub user a day; this is request %d in the last "
                         "24 hours. Edit this issue tomorrow to try again" % (DAILY_CAP, len(reqs)))
        return out
    out["status"] = "build"
    return out


def custom_manifest(build_dir, out_dir, off, release, fingerprint, partitions_csv=None, size_tool=None,
                    now=None):
    """buildcost.manifest for a custom build, plus the "built" time ghpages.prune ages it by."""
    off = buildopts.resolve(off)
    if not ghpages.TAG.match(release or "") or buildopts.fingerprint(off, release) != fingerprint:
        raise ValueError("the switches and release do not give fingerprint %s" % fingerprint)
    m = buildcost.manifest(build_dir, out_dir, "custom", off, release, partitions_csv=partitions_csv,
                           size_tool=size_tool)
    m["built"] = _now(now).strftime(STAMP)
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(m, fh, indent=1)
        fh.write("\n")
    return m


def _check_build(src, fingerprint, partitions_csv):
    """Refuse a build folder that is not exactly what custom_manifest makes."""
    with open(os.path.join(src, "manifest.json"), encoding="utf-8") as fh:
        m = json.load(fh)
    parts = buildcost.partitions(partitions_csv or os.path.join(ROOT, "partitions.csv"))
    app_off, app_size = parts["app0"]
    if m.get("app_partition") != {"offset": app_off, "size": app_size}:
        raise ValueError("the manifest's app partition is not app0")
    off = m.get("off")
    release = m.get("release")
    if not isinstance(off, list) or not all(isinstance(i, str) for i in off) or not isinstance(release, str):
        raise ValueError("the manifest has no switch list or release")
    buildopts.resolve(set(off))
    if not ghpages.TAG.match(release) or m.get("fingerprint") != fingerprint \
            or buildopts.fingerprint(set(off), release) != fingerprint:
        raise ValueError("the manifest does not match fingerprint %s" % fingerprint)
    if not isinstance(m.get("built"), str) or not re.match(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$", m["built"]):
        raise ValueError("the manifest has no built time")
    offsets = {"otadata": parts["otadata"][0], "app": app_off}
    files = []
    roles = []
    for img in m.get("images", []):
        role = img.get("role")
        names, offset = ROLES.get(role, ((), None))
        if img.get("file") not in names or img.get("offset") != (offset or offsets.get(role)):
            raise ValueError("unexpected image in the manifest")
        path = os.path.join(src, img["file"])
        if not os.path.isfile(path) or os.path.getsize(path) != img.get("size") \
                or buildcost._sha256(path) != img.get("sha256"):
            raise ValueError("%s does not match the manifest" % img["file"])
        files.append(img["file"])
        roles.append(role)
    if sorted(roles) != sorted(set(roles)) or not {"bootloader", "partitions", "app"} <= set(roles):
        raise ValueError("the manifest does not list the images a board needs")
    app = [img for img in m["images"] if img["role"] == "app"][0]
    if app["size"] > app_size:
        raise ValueError("firmware.bin is %d bytes, app0 holds %d" % (app["size"], app_size))
    if sorted(os.listdir(src)) != sorted(files + ["manifest.json"]):
        raise ValueError("the build folder holds files the manifest does not list")
    return files + ["manifest.json"]


def publish(src, gh_pages_dir, fingerprint, builds_cap=ghpages.BUILDS_CAP, partitions_csv=None):
    """Copy a checked build to builds/<fingerprint>/ and prune. Returns the removed folders."""
    if not isinstance(fingerprint, str) or not FINGERPRINT.match(fingerprint):
        raise ValueError("not a fingerprint: %r" % (fingerprint,))
    names = _check_build(src, fingerprint, partitions_csv)
    dst = os.path.join(gh_pages_dir, "builds", fingerprint)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    for name in names:
        shutil.copyfile(os.path.join(src, name), os.path.join(dst, name))
    return ghpages.prune(gh_pages_dir, builds_cap=builds_cap)


def pages_url(repo):
    owner, name = repo.split("/", 1)
    return "https://%s.github.io/%s/" % (owner.lower(), name)


def _labels(off, features):
    names = {f["id"]: f["label"] for f in features}
    got = [names[i] for i in off.split(",") if i in names]
    return ", ".join(got) or "nothing"


def answer(parse_result, status, reason, fingerprint, target, release, off, build_result, publish_result,
           repo, run_url, features=None):
    """(comment text, close the issue?) for the end of a run."""
    if features is None:
        features = buildopts.load(ROOT)[0]
    site = pages_url(repo) if REPO.match(repo or "") else ""
    failed = ("This AXIS Micro BBS build request did not finish. The run is here:\n%s\n\n"
              "Rev will take a look; you can also edit the issue to try again." % run_url)
    if parse_result != "success":
        return failed, False
    if status == "done":
        return ("This issue already has its AXIS Micro BBS answer above. For another combination, "
                "open a new Build request."), True
    if status == "refused":
        return ("This AXIS Micro BBS build request was not built: %s.\n\n"
                "Edit the issue (tick or untick the form's boxes) to try again." % reason), False
    if status == "preset":
        return ("No build needed: %s (release %s).\n\n%sinstall.html\n" % (reason, release, site)), True
    built = status == "build" and build_result == "success" and publish_result == "success"
    if status != "exists" and not built:
        return failed, False
    if not FINGERPRINT.match(fingerprint or ""):
        return failed, False
    head = ("Your AXIS Micro BBS build is ready." if built else
            "This combination was built before, so it is ready now.")
    board = TARGETS.get(target, "")
    text = (
        "%s\n\n"
        "Release: %s\n"
        "Left out: %s\n"
        "Board: %s\n\n"
        "Open this link in Chrome or Edge on a PC with the board plugged in by USB:\n"
        "%sinstall.html?build=%s\n\n"
        "A CYD and a bare ESP32 run the same image. On a bare ESP32, set the board to headless "
        "over serial after flashing.\n" % (head, release, _labels(off, features), board, site, fingerprint))
    return text, True


def _write_outputs(path, values):
    with open(path, "a", encoding="utf-8", newline="\n") as fh:
        for key in ("status", "reason", "off", "target", "release", "fingerprint"):
            value = values[key]
            if "\n" in value or "\r" in value:
                raise ValueError("output %s is not one line" % key)
            fh.write("%s=%s\n" % (key, value))


def _read_json(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def main(argv=None, api=None, now=None):
    ap = argparse.ArgumentParser(prog="build_request")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--event", required=True)
    c.add_argument("--index", required=True)
    c.add_argument("--builds", required=True)
    c.add_argument("--out", required=True)
    c.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    m = sub.add_parser("manifest")
    m.add_argument("build_dir")
    m.add_argument("out_dir")
    m.add_argument("--off", default="")
    m.add_argument("--release", required=True)
    m.add_argument("--fingerprint", required=True)
    m.add_argument("--partitions")
    p = sub.add_parser("publish")
    p.add_argument("--from", dest="src", required=True)
    p.add_argument("--fingerprint", required=True)
    p.add_argument("--into", default=os.environ.get("GHP", ""))
    a = sub.add_parser("answer")
    for name in ("parse-result", "status", "reason", "fingerprint", "target", "release", "off",
                 "build-result", "publish-result", "repo", "run-url"):
        a.add_argument("--" + name, default="")
    a.add_argument("--body", required=True)
    args = ap.parse_args(argv)
    try:
        if args.cmd == "check":
            if not REPO.match(args.repo):
                raise ValueError("no repository name")
            event = _read_json(args.event)
            if not isinstance(event, dict):
                raise ValueError("cannot read the event file")
            with open(args.builds, encoding="utf-8") as fh:
                builds = [line.strip() for line in fh if line.strip()]
            if api is None:
                api = github_api(os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN") or "")
            got = decide(event, _read_json(args.index), builds,
                         lambda user: fetch_requests(api, args.repo, user, now=now),
                         lambda number: live_issue(api, args.repo, number), now=now)
            _write_outputs(args.out, got)
            print("status %s, fingerprint %s, release %s, off %s" % (
                got["status"], got["fingerprint"] or "none", got["release"] or "none", got["off"] or "none"))
            if got["reason"]:
                print("reason: " + got["reason"])
        elif args.cmd == "manifest":
            off = {s for s in args.off.split(",") if s}
            got = custom_manifest(args.build_dir, args.out_dir, off, args.release, args.fingerprint,
                                  partitions_csv=args.partitions)
            print("build %s: app %d bytes, static RAM %d bytes" % (
                got["fingerprint"], got["cost"]["flash"], got["cost"]["dram_static"]))
        elif args.cmd == "publish":
            if not args.into:
                raise ValueError("no target folder (--into or GHP)")
            gone = publish(args.src, args.into, args.fingerprint)
            print("build %s added" % args.fingerprint)
            for path in gone:
                print("removed " + path)
        else:
            text, close = answer(args.parse_result, args.status, args.reason, args.fingerprint, args.target,
                                 args.release, args.off, args.build_result, args.publish_result, args.repo,
                                 args.run_url)
            with open(args.body, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            print("close" if close else "open")
    except (OSError, ValueError, KeyError) as e:
        print("build_request: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
