# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Turn a cppcheck XML (version 2) report into SARIF for GitHub code scanning.

    python3 tools/ci/cppcheck_sarif.py cppcheck.xml cppcheck.sarif

Only findings in src/ are kept. Generated art headers (*_art.h, rotatocam_splash.h) and
third_party/ are dropped. cppcheck "error" becomes a SARIF error (a new one fails the pull
request's code scanning check); warning, performance and portability become warnings; the
rest are notes. Also prints each finding as path:line: severity: message, and writes a
Markdown table to $GITHUB_STEP_SUMMARY when that is set.
"""
import fnmatch
import json
import os
import sys
import xml.etree.ElementTree as ET

KEEP = ["src/*"]
DROP = ["*_art.h", "src/bbs/rotatocam_splash.h", "third_party/*"]
LEVEL = {"error": "error", "warning": "warning", "performance": "warning", "portability": "warning"}


def rel(path, root):
    p = os.path.normpath(path)
    if os.path.isabs(p):
        p = os.path.relpath(p, root)
    return p.replace(os.sep, "/")


def wanted(path):
    return any(fnmatch.fnmatch(path, k) for k in KEEP) and not any(fnmatch.fnmatch(path, d) for d in DROP)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    root = os.getcwd()
    tree = ET.parse(src)
    ver = tree.find("cppcheck")
    version = ver.get("version", "") if ver is not None else ""
    rules, results, lines = {}, [], []
    for err in tree.iter("error"):
        locs = err.findall("location")
        if not locs:
            continue  # whole-run notes (missing includes, check level), no source line
        first = locs[0]
        path = rel(first.get("file", ""), root)
        if not wanted(path):
            continue
        rid = err.get("id", "unknown")
        sev = err.get("severity", "style")
        msg = err.get("verbose") or err.get("msg") or rid
        line = max(1, int(first.get("line", "1") or 1))
        col = int(first.get("column", "0") or 0)
        if rid not in rules:
            rule = {"id": rid, "shortDescription": {"text": err.get("msg", rid)},
                    "properties": {"tags": ["cppcheck", sev]}}
            if err.get("cwe"):
                rule["properties"]["tags"].append("external/cwe/cwe-" + err.get("cwe"))
            rules[rid] = rule
        region = {"startLine": line}
        if col > 0:
            region["startColumn"] = col
        results.append({
            "ruleId": rid,
            "level": LEVEL.get(sev, "note"),
            "message": {"text": msg},
            "locations": [{"physicalLocation": {
                "artifactLocation": {"uri": path, "uriBaseId": "%SRCROOT%"},
                "region": region}}],
        })
        lines.append((path, line, sev, rid, err.get("msg", "")))
    sarif = {
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "version": "2.1.0",
        "runs": [{
            "tool": {"driver": {"name": "cppcheck", "version": version,
                                "informationUri": "https://cppcheck.sourceforge.io/",
                                "rules": list(rules.values())}},
            "results": results,
        }],
    }
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(sarif, f, indent=1)
    lines.sort()
    for path, line, sev, rid, msg in lines:
        print(f"{path}:{line}: {sev}: {msg} [{rid}]")
    print(f"cppcheck {version}: {len(lines)} findings in src/")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(f"### cppcheck {version}: {len(lines)} findings in src/\n\n")
            if lines:
                f.write("| File | Severity | Check | Message |\n|-|-|-|-|\n")
                for path, line, sev, rid, msg in lines:
                    f.write(f"| {path}:{line} | {sev} | {rid} | {msg.replace('|', '/')} |\n")


if __name__ == "__main__":
    main()
