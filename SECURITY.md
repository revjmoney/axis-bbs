# Security policy

## Supported versions

Only the latest `main` (and the newest release built from it) gets security fixes. Older tags
and builds are not patched; update to the latest build instead.

## Reporting a vulnerability

Please report security problems privately, not in a public issue, discussion or pull request.

1. Open the repository's **Security** tab.
2. Click **Report a vulnerability** (GitHub's private vulnerability reporting).
3. Describe the problem: what an attacker can do, which part of the firmware or tools it is in
   (telnet, logins and passwords, file transfers, cluster links, the setup portal, doors), how
   to reproduce it, and the version or commit you tested.

Only the maintainer sees the report. You will get an answer in the report thread, normally
within a week. Once a fix is on `main`, the advisory is published with credit to you unless
you ask to stay anonymous.

Please do not test against boards you do not own, and do not include real passwords, WiFi keys
or cluster keys in a report.
