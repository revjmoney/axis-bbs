# Cluster, round 1: second board joins, shared files and users, one address, chat

**Goal:** Rev's second CYD (no SD card) joins the first as node 2: callers dialing `axis.local` reach a free node,
both nodes share the file areas and the user base, callers can chat across nodes, and the main fails over.

**Spec:** `docs/superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md` section 5.11.

## Decisions for this round (Rev: "Yes, cluster + chat")

- **WiFi first:** a board with no saved network shows a touch WiFi picker with an on-screen keyboard; Rev types
  the password on the board (never Claude). Saved in NVS `net` s0..s4 / p0..p4 like the first board.
- **Discovery:** UDP broadcast on port 2325 (`AXIS?` / `AXIS! main <ip> epoch <n>`); no answer in 3 s: become main.
  Node numbers are kept per board in NVS. Heartbeat every second; 5 s without one: the lowest surviving node with a
  complete user replica becomes main and bumps the epoch.
- **Link:** one TCP connection from each follower to the main (port 2324), length-prefixed frames.
- **Shared data:** the main owns the SD card. A follower's FileLibrary is remote (list, read, open/readAt, create/
  write, describe, remove, rename go over the link). The user base is small, so every board keeps a full copy:
  the main sends the whole users file after each change; a follower sends its changes (new application, a call,
  validate/deny) to the main.
- **One address (deviation, ruled):** the main keeps `axis.local`. When it is busy it relays a new caller to a free
  follower (TCP pass-through). Simpler than answering mDNS with another board's IP; the cost is that a relayed
  caller depends on the main staying up. Direct-to-node mDNS stays on the list.
- **Chat:** `[T]` teleconference across all nodes, `[W]` who's online cluster-wide, `[P]` page the sysop (shown on
  every CYD). Messages go through the main.
- **Later:** doors on a follower (needs the main's door files over the link), full file-area replicas on followers,
  the split-brain quarantine log.

## Tasks

1. Touch WiFi picker + on-screen keyboard (portable logic, PC tests; firmware draws; `@WIFIPICK` bench command).
2. Cluster protocol: frames, discovery, election, heartbeat, failover (portable, PC tests over in-memory links).
3. Remote FileLibrary and user-base sync over the link (PC tests).
4. Relay of callers from a busy main to a free follower.
5. Chat: teleconference, who's online, page sysop (PC tests with two nodes on one link).
6. Two-board test: flash board 2, join, browse files from node 2, chat across nodes, unplug the main.
