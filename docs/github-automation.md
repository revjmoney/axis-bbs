# GitHub automation and security checks

Everything that runs on GitHub for this repo, what it checks, and what you see. No workflow
needs a repository secret. Every third party action is pinned to a full commit SHA with the
version in a comment; Dependabot keeps those pins current.

## The checks

| Workflow (file) | Runs on | What it does | Where you see it |
|---|---|---|---|
| Build (`build.yml`) | every push, every pull request | `host`: host build, all doctest tests, menu art check. `firmware`: `pio run -e cyd` and `-e headless`, size report, and the CYD firmware as a download | Actions tab, the check marks on commits and pull requests |
| CodeQL (`codeql.yml`) | push to main, pull requests, Mondays, by hand | GitHub's security analysis of the C/C++ host build and the Python in `tools/` | Security tab, Code scanning; on pull requests a "Code scanning results" check and comments on changed lines |
| Sanitizers (`sanitizers.yml`) | push to main, pull requests, by hand | Host tests built with AddressSanitizer and UndefinedBehaviorSanitizer | Actions tab; the log names file, line and stack of each finding |
| Static analysis (`static-analysis.yml`) | push to main, pull requests, Mondays, by hand | cppcheck over `src/` | Security tab, Code scanning, tool "cppcheck"; a table in the run's Summary |
| Release (`release.yml`) | pushing a tag `v*` | Builds both envs, publishes a GitHub Release with the .bin files | Releases page |
| Fortify AST Scan (`fortify.yml`) | push to main, pull requests, Thursdays | Skips itself (notice in the log) until Fortify on Demand secrets exist | Actions tab |
| Pages (`pages.yml`) | push to main touching `site/` | Publishes the website | Pages URL |
| Dependabot (`dependabot.yml`) | Mondays | One grouped pull request bumping every GitHub Action with a new release | Pull requests, titled `ci: bump ...` |

### Build

The same two jobs as before (`host` and `firmware`), now with:

* apt installs that retry up to 4 times with a pause (`tools/ci/apt_install.sh`). GitHub's
  runners sometimes cannot reach the Ubuntu mirrors for a minute; that used to fail the job.
* The CYD firmware as a workflow artifact on every push and pull request. Open the run in the
  Actions tab, scroll to **Artifacts**, and download `axis-bbs-cyd-<commit>`. It holds
  `firmware.bin`, `bootloader.bin` and `partitions.bin`. Kept 30 days.

### CodeQL

Compiles the host build (the same code the tests cover: BBS, doors, LORD, cluster, telnet,
transfers) while CodeQL watches, then runs GitHub's security queries. Python in `tools/` is
scanned without building. ESP32 only files (`src/platform/cyd`, `src/main.cpp`) need ESP-IDF
and are not in the host build, so CodeQL does not see them.

Query suite: the **default** one (security queries). The `security-and-quality` suite adds
maintainability queries that mostly flag deliberate low level code here (8086 emulator, fixed
buffers, packed protocols); cppcheck covers code quality instead, so the Security tab stays
about security.

### Sanitizers

Builds the host tests with `-fsanitize=address,undefined -fno-sanitize-recover=all` (flags on
the cmake command line, the CMakeLists files are untouched) and runs `axis_tests`. Any out of
bounds access, use after free, leak, bad shift or overflow stops the run with the file, line
and a stack trace in the log.

The job is marked `continue-on-error: true` for now because main has two known findings (below).
The run shows the job as failed (red cross on the job) but the workflow still passes. Remove
the `continue-on-error` line once both are fixed, so any new finding fails the check.

Known findings, found locally with UBSan (MinGW g++ 16.1, trap mode), confirmed with gdb:

1. `src/bbs/theme.cpp:372` in `theme::window`: when drawing the first drip row (`drip1`, 72
   characters) the code also reads `drip2[i]`, but `drip2` is only 70 bytes. At `i == 71` that
   reads past the end of `drip2` (out of bounds read of a static array). Reached by the
   STATS window (`Node::showSystemInfo`, `tests/test_node.cpp:94`). Fix: treat `i` past the end
   of `drip2` as "not a drip", for example `i >= (int)sizeof drip2 || drip2[i] != '#'`.
2. `third_party/sha256/sha256.c:49` in `sha256_transform`: `data[j] << 24` shifts a byte
   promoted to signed `int` into the sign bit, which is undefined in C for bytes 0x80 and up.
   Reached by every password hash (`pbkdf2Sha256Hex`, `tests/test_accounts.cpp:97`). Fix:
   cast each byte to `WORD` before shifting.

With both patched (in a scratch copy only), all 793 test cases pass under UBSan. AddressSanitizer
does not work with MinGW on Windows, so ASan and LeakSanitizer have only run in CI.

### Static analysis (cppcheck)

Runs Ubuntu's cppcheck on every `src/` file in the host build (from CMake's
`compile_commands.json`), with the warning, performance and portability checks on top of the
always on error checks. Generated art headers (`*_art.h`, `rotatocam_splash.h`) and
`third_party/` are dropped by `tools/ci/cppcheck_sarif.py`, which converts the report to SARIF
and uploads it to code scanning.

The existing backlog never fails anything: the cppcheck step always passes, and code scanning
compares a pull request with main, so a pull request only shows the findings it adds. Only a
new cppcheck **error** (not a warning) fails the pull request's code scanning check. Existing
findings are listed in the Security tab, where each can be fixed or dismissed.

### Release

```
git tag -a v0.52 -m "AXIS BBS 0.52: what changed"
git push origin v0.52
```

The tag message becomes the release notes (a tag without a message gets GitHub's generated
list of changes). The release gets, for `cyd` and `headless`: `firmware.bin`,
`bootloader.bin`, `partitions.bin`, plus `flasher_args.json` (the flash offsets) when ESP-IDF
writes it, and a `SHA256SUMS.txt`. The build does not produce a merged (factory) image, so
none is attached. Pushing the same tag again (after deleting it) replaces the files.

### Hardening on every workflow

* Top level `permissions: contents: read`. Jobs that need more ask for it themselves:
  code scanning upload (`security-events: write`), Pages deploy (`pages`, `id-token`), and
  the release job (`contents: write`).
* `concurrency`: a new push to the same branch or pull request cancels the older run
  (the release workflow never cancels).
* `timeout-minutes` on every job.

### Repo files

* `SECURITY.md`: report vulnerabilities privately through the Security tab's
  "Report a vulnerability"; only the latest main is supported.
* `.github/pull_request_template.md`: checklist (tests pass, firmware builds, no secrets, no
  AI attribution lines in commits, licence).

## Repository settings to turn on afterwards

Read only checks done 2026-11-11: private vulnerability reporting is already **enabled**;
CodeQL default setup is **not configured**; secret scanning, push protection and Dependabot
security updates are on; non provider patterns and validity checks are off; there are no
rulesets and no branch protection; default workflow token permission is already `read`.
The repo is public and owned by a user account (not an organization).

Run these after the `ci-checks` branch is merged to main.

1. Keep CodeQL default setup **off**. It conflicts with `codeql.yml` (GitHub rejects the
   workflow's uploads while default setup is on). Check it says `not-configured`:

   ```
   gh api repos/revjmoney/axis-bbs/code-scanning/default-setup -q .state
   ```

2. Private vulnerability reporting (already on; this is safe to repeat):

   ```
   gh api -X PUT repos/revjmoney/axis-bbs/private-vulnerability-reporting
   ```

3. Secret scanning for non provider patterns (private keys, connection strings and similar)
   and validity checks (GitHub tests whether a leaked token is still live):

   ```
   gh api -X PATCH repos/revjmoney/axis-bbs --input - <<'EOF'
   {"security_and_analysis": {
     "secret_scanning_non_provider_patterns": {"status": "enabled"},
     "secret_scanning_validity_checks": {"status": "enabled"}}}
   EOF
   ```

   If GitHub answers that a feature is not available for this plan, leave that one off.

4. Dependabot alerts (security updates already need them; this makes sure):

   ```
   gh api -X PUT repos/revjmoney/axis-bbs/vulnerability-alerts
   ```

5. Branch ruleset for main: pull requests must pass the Build checks (`host` and `firmware`)
   before merging; force pushes and deleting main are blocked; Rev (repository admin) can still
   push straight to main. A required status check also applies to direct pushes, so the
   admin bypass with `bypass_mode: always` is what keeps direct pushes working. GitHub then
   shows "Bypassed rule violations" on the push, which is expected.

   ```
   gh api -X POST repos/revjmoney/axis-bbs/rulesets --input - <<'EOF'
   {
     "name": "main",
     "target": "branch",
     "enforcement": "active",
     "conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
     "bypass_actors": [
       {"actor_type": "RepositoryRole", "actor_id": 5, "bypass_mode": "always"}
     ],
     "rules": [
       {"type": "deletion"},
       {"type": "non_fast_forward"},
       {"type": "required_status_checks",
        "parameters": {
          "strict_required_status_checks_policy": false,
          "required_status_checks": [
            {"context": "host", "integration_id": 15368},
            {"context": "firmware", "integration_id": 15368}
          ]}}
     ]
   }
   EOF
   ```

   `actor_id` 5 is the built in Admin repository role, `integration_id` 15368 is GitHub
   Actions. Not yet tested on this user owned repo. After creating it, confirm with
   `gh api repos/revjmoney/axis-bbs/rulesets` and one direct test push to main. If the push is
   refused, drop the `required_status_checks` rule (keep the other two) or open the ruleset
   in Settings > Rules > Rulesets and add "Repository admin" to the bypass list there.
   The check names are the job names in `build.yml`; renaming those jobs breaks this rule.
   Later, once the sanitizer findings are fixed, `asan-ubsan` can be added the same way.

6. Optional: require every action to be pinned to a full commit SHA (this repo already
   complies, so it only guards future edits):

   ```
   gh api -X PUT repos/revjmoney/axis-bbs/actions/permissions --input - <<'EOF'
   {"enabled": true, "allowed_actions": "all", "sha_pinning_required": true}
   EOF
   ```

   If GitHub rejects `sha_pinning_required`, use Settings > Actions > General instead.

Do not enable CodeQL default setup, and do not add Fortify secrets unless you want that scan
to start running.
