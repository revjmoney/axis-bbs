"""Tests for tools/ghpages.py, tools/ci/ghpages_publish.sh and the two publishing workflows.
Run: python -I tools/test_ghpages.py"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import ghpages  # noqa: E402

MB = 1024 * 1024


def write(path, data=b"x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)


def tree(root):
    out = []
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d != ".git"]
        for f in files:
            out.append(os.path.relpath(os.path.join(dirpath, f), root).replace(os.sep, "/"))
    return sorted(out)


def add_build(gh, fp, built, size):
    d = os.path.join(gh, "builds", fp)
    write(os.path.join(d, "firmware.bin"), b"\0" * size)
    with open(os.path.join(d, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump({"built": built}, fh)


class MergeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.site = os.path.join(self.tmp, "site")
        self.gh = os.path.join(self.tmp, "gh")
        write(os.path.join(self.site, "index.html"), b"new")
        write(os.path.join(self.site, "media", "a.png"))
        write(os.path.join(self.gh, "index.html"), b"old")
        write(os.path.join(self.gh, "gone.html"))
        write(os.path.join(self.gh, "media", "stale.png"))
        write(os.path.join(self.gh, "releases", "v1", "everything", "firmware.bin"))
        write(os.path.join(self.gh, "releases", "index.json"), b"{}")
        write(os.path.join(self.gh, "builds", "abc", "firmware.bin"))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_merge_keeps_releases_and_builds(self):
        ghpages.merge(self.site, self.gh)
        t = tree(self.gh)
        self.assertIn("releases/v1/everything/firmware.bin", t)
        self.assertIn("releases/index.json", t)
        self.assertIn("builds/abc/firmware.bin", t)

    def test_merge_mirrors_the_site(self):
        ghpages.merge(self.site, self.gh)
        t = tree(self.gh)
        self.assertNotIn("gone.html", t)
        self.assertNotIn("media/stale.png", t)
        self.assertIn("media/a.png", t)
        self.assertIn(".nojekyll", t)
        with open(os.path.join(self.gh, "index.html"), "rb") as fh:
            self.assertEqual(fh.read(), b"new")

    def test_merge_copies_the_installer_lists(self):
        inst = os.path.join(self.tmp, "installer")
        write(os.path.join(inst, "features.json"), b"[]")
        write(os.path.join(inst, "presets.json"), b"[]")
        write(os.path.join(inst, "test-fixtures", "x.json"), b"[]")
        ghpages.merge(self.site, self.gh, installer_dir=inst)
        t = tree(self.gh)
        self.assertIn("installer/features.json", t)
        self.assertIn("installer/presets.json", t)
        self.assertNotIn("installer/test-fixtures/x.json", t)

    def test_site_with_a_reserved_folder_is_refused(self):
        write(os.path.join(self.site, "releases", "x"))
        with self.assertRaises(ValueError):
            ghpages.merge(self.site, self.gh)


class PruneTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.gh = os.path.join(self.tmp, "gh")
        for tag in ("v0.9", "v0.52", "v0.60", "v0.100", "v0.8"):
            write(os.path.join(self.gh, "releases", tag, "everything", "firmware.bin"))
        add_build(self.gh, "aaa", "2026-11-01T00:00:00Z", 2 * MB)
        add_build(self.gh, "bbb", "2026-11-03T00:00:00Z", 2 * MB)
        add_build(self.gh, "ccc", "2026-11-02T00:00:00Z", 2 * MB)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_releases_sort_by_version_number(self):
        self.assertEqual(ghpages.releases(self.gh), ["v0.100", "v0.60", "v0.52", "v0.9", "v0.8"])

    def test_prune_keeps_the_three_newest_releases(self):
        ghpages.prune(self.gh, keep_releases=3, builds_cap=300 * MB)
        self.assertEqual(sorted(os.listdir(os.path.join(self.gh, "releases"))),
                         sorted(["v0.100", "v0.60", "v0.52", "index.json"]))

    def test_prune_twice_gives_the_same_tree(self):
        ghpages.prune(self.gh, keep_releases=3, builds_cap=5 * MB)
        first = tree(self.gh)
        ghpages.prune(self.gh, keep_releases=3, builds_cap=5 * MB)
        self.assertEqual(tree(self.gh), first)

    def test_prune_drops_the_oldest_builds_past_the_cap(self):
        ghpages.prune(self.gh, keep_releases=3, builds_cap=5 * MB)
        self.assertEqual(sorted(os.listdir(os.path.join(self.gh, "builds"))), ["bbb", "ccc"])

    def test_build_without_a_date_counts_as_oldest(self):
        write(os.path.join(self.gh, "builds", "zzz", "firmware.bin"), b"\0" * MB)
        ghpages.prune(self.gh, keep_releases=3, builds_cap=6 * MB + 1024)  # 1 KB for the manifests
        self.assertEqual(sorted(os.listdir(os.path.join(self.gh, "builds"))), ["aaa", "bbb", "ccc"])

    def test_index_lists_latest_and_previous_after_prune(self):
        ghpages.prune(self.gh, keep_releases=3, builds_cap=300 * MB)
        with open(os.path.join(self.gh, "releases", "index.json"), encoding="utf-8") as fh:
            idx = json.load(fh)
        self.assertEqual(idx, {"latest": "v0.100", "previous": "v0.60",
                               "releases": ["v0.100", "v0.60", "v0.52"]})

    def test_add_release_copies_prunes_and_indexes(self):
        src = os.path.join(self.tmp, "dl")
        write(os.path.join(src, "everything", "manifest.json"), b"{}")
        write(os.path.join(src, "costs.json"), b"{}")
        ghpages.add_release(self.gh, "v0.101", src)
        self.assertIn("releases/v0.101/everything/manifest.json", tree(self.gh))
        self.assertIn("releases/v0.101/costs.json", tree(self.gh))
        self.assertEqual(ghpages.releases(self.gh), ["v0.101", "v0.100", "v0.60"])

    def test_bad_tag_is_refused(self):
        src = os.path.join(self.tmp, "dl")
        os.makedirs(src)
        for tag in ("../x", "v1/../../x", "", "v1 x", "$(id)"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                ghpages.add_release(self.gh, tag, src)


def _sh():
    for cand in (shutil.which("sh"), r"C:\Program Files\Git\usr\bin\sh.exe", r"C:\Program Files\Git\bin\sh.exe"):
        if cand and os.path.isfile(cand):
            return cand
    return None


@unittest.skipUnless(_sh() and shutil.which("git"), "needs sh and git")
class PublishScriptTest(unittest.TestCase):
    """tools/ci/ghpages_publish.sh against a local bare repo standing in for GitHub."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.origin = os.path.join(self.tmp, "origin.git")
        self.work = os.path.join(self.tmp, "work")
        self.git("init", "-q", "--bare", self.origin, cwd=self.tmp)
        self.git("init", "-q", self.work, cwd=self.tmp)
        self.git("remote", "add", "origin", self.origin)
        self.env = dict(os.environ, GHP_TMP=os.path.join(self.tmp, "t"), GHP_RETRY_SLEEP="0",
                        GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
                        GIT_COMMITTER_EMAIL="t@t")

    def tearDown(self):
        def onerror(func, path, _exc):
            os.chmod(path, 0o700)
            func(path)
        shutil.rmtree(self.tmp, onerror=onerror)

    def git(self, *args, cwd=None):
        return subprocess.run(["git", *args], cwd=cwd or self.work, check=True,
                              capture_output=True, text=True).stdout.strip()

    def publish(self, msg, files):
        """Run the script with a command that writes `files` ({path: text}) into $GHP."""
        script = os.path.join(ROOT, "tools", "ci", "ghpages_publish.sh")
        writer = os.path.join(self.tmp, "writer.py")
        with open(writer, "w", encoding="utf-8") as fh:
            fh.write("import json, os, sys\n"
                     "for k, v in json.loads(sys.argv[1]).items():\n"
                     "    p = os.path.join(os.environ['GHP'], k)\n"
                     "    os.makedirs(os.path.dirname(p), exist_ok=True)\n"
                     "    open(p, 'w').write(v)\n")
        return subprocess.run([_sh(), script, msg, sys.executable, "-I", writer, json.dumps(files)],
                              cwd=self.work, env=self.env, capture_output=True, text=True)

    def test_first_publish_creates_the_branch_and_later_ones_keep_one_commit(self):
        r = self.publish("one", {"index.html": "a", "releases/v1/x": "1"})
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = self.publish("two", {"builds/abc/y": "2"})
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        files = self.git("ls-tree", "-r", "--name-only", "gh-pages", cwd=self.origin).split()
        self.assertEqual(sorted(files), ["builds/abc/y", "index.html", "releases/v1/x"])
        self.assertEqual(self.git("rev-list", "--count", "gh-pages", cwd=self.origin), "1")
        self.assertEqual(self.git("log", "-1", "--format=%s", "gh-pages", cwd=self.origin), "two")

    def test_nothing_changed_pushes_nothing(self):
        self.publish("one", {"index.html": "a"})
        before = self.git("rev-parse", "gh-pages", cwd=self.origin)
        r = self.publish("again", {"index.html": "a"})
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("nothing changed", r.stdout)
        self.assertEqual(self.git("rev-parse", "gh-pages", cwd=self.origin), before)

    def test_tree_without_index_html_is_refused(self):
        r = self.publish("bad", {"other.html": "a"})
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(subprocess.run(["git", "rev-parse", "--verify", "-q", "gh-pages"], cwd=self.origin,
                                        capture_output=True).returncode, 1)


SHA_PIN = re.compile(r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$")


def load_yaml(path):
    """PyYAML when it is installed; otherwise a structural check of the text."""
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    try:
        import yaml  # noqa: F401
    except ImportError:
        return text, None
    return text, yaml.safe_load(text)


class WorkflowTest(unittest.TestCase):
    FILES = ("pages.yml", "release.yml")

    def path(self, name):
        return os.path.join(ROOT, ".github", "workflows", name)

    def test_every_action_is_pinned_by_sha(self):
        for name in self.FILES:
            text, _ = load_yaml(self.path(name))
            uses = re.findall(r"^\s*(?:-\s+)?uses:\s*(\S+)", text, re.M)
            self.assertTrue(uses, name)
            for u in uses:
                with self.subTest(workflow=name, uses=u):
                    self.assertRegex(u, SHA_PIN)

    def test_no_tabs_and_no_em_dashes(self):
        for name in self.FILES:
            text, _ = load_yaml(self.path(name))
            with self.subTest(workflow=name):
                self.assertNotIn("\t", text)
                self.assertNotIn("\u2014", text)

    def test_structure(self):
        for name in self.FILES:
            text, doc = load_yaml(self.path(name))
            with self.subTest(workflow=name):
                if doc is None:
                    for key in ("name:", "jobs:", "permissions:"):
                        self.assertRegex(text, r"(?m)^" + key)
                    continue
                self.assertIn("jobs", doc)
                self.assertTrue(True in doc or "on" in doc)  # PyYAML reads `on:` as True
                self.assertEqual(doc["permissions"], {"contents": "read"})
                for jid, job in doc["jobs"].items():
                    self.assertIn("runs-on", job, jid)
                    self.assertIn("timeout-minutes", job, jid)
                    for step in job.get("steps", []):
                        self.assertTrue("run" in step or "uses" in step, (jid, step))

    def test_pages_pushes_gh_pages_and_no_longer_deploys_an_artifact(self):
        text, doc = load_yaml(self.path("pages.yml"))
        self.assertIn("ghpages_publish.sh", text)
        self.assertIn("tools/ghpages.py site", text)
        self.assertNotIn("deploy-pages", text)
        self.assertIn("installer/*.json", text)
        if doc:
            self.assertEqual(doc["jobs"]["publish"]["permissions"], {"contents": "write"})

    def test_release_builds_every_preset_and_publishes(self):
        text, doc = load_yaml(self.path("release.yml"))
        for needle in ("AXIS_PRESET", "buildcost.py manifest", "buildcost.py table", "sdkit.py",
                       "tools/ghpages.py release", "ghpages_publish.sh", "gh release create",
                       "installer/presets.json"):
            self.assertIn(needle, text)
        if doc:
            jobs = doc["jobs"]
            self.assertEqual(set(jobs), {"presets", "build", "release", "publish"})
            self.assertEqual(jobs["build"]["permissions"], {"contents": "read"})
            self.assertIn("fromJSON(needs.presets.outputs.list)", str(jobs["build"]["strategy"]["matrix"]))
            self.assertEqual(set(jobs["publish"]["needs"]), {"build", "release"})
            self.assertEqual(jobs["publish"]["permissions"], {"contents": "write"})


if __name__ == "__main__":
    unittest.main()
