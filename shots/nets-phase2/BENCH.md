# Nets Phase 2 bench, 2026-11-16

Board 1 (MAC checked before writing) flashed with nets/phase2 at f426af0 (image 87.4% of the app slot). Board 2 was not connected, so its steps were skipped. The PC test peer `axis_netpeer` ran on the build box with `user on`.

| Step | Result |
| --- | --- |
| 1 Flash board 1 | Written and verified, boots, `net on`, history done. |
| 2 Peer and first peer | Board 1's `net_first_peer` pointed at the peer; `@NET SYNC` synced with BenchPeer. |
| 3 Board applies | `@NETAPPLY` gave `@OK applied`; the peer's `apps` shows board 1 pending, yes 0 no 0. Screenshot 1 (System > User Net, Status pending). |
| 4 Peer applies, board not a member | The board holds both applications; `@NETVOTE <peer> yes` gives `@ERR this board is not a member`; the User Net page has no New Board Voting row (screenshot 1). |
| 5 Rev's key (admit, veto) | Skipped: needs Rev to type `@NETKEY SET` himself. Covered by the host tests. |
| 6 Record browser | Votes lists both applications (screenshot 2). FIND on the peer's id lists its card, app and sessions. SHOW by token gives every field and `check signed by <peer>, checks` (screenshots 3 and 4). A token with a wrong time gives `that line is not in the record now`. |
| 7 Heap | Largest block during a sync 106 KB (window `wlarge`), sync lines report 64 to 104 KB largest; no crash. The lowest free heap since boot was 32.7 KB in the view cache while screenshots were being taken during a sync. |
| 8 Restore | `net_user=0`, `net_first_peer` cleared, peer stopped. The test applications stay in the record and `/net/apps.txt` (append only). |

Notes:
- With `net_user=0` the `@NET` status line still says `user net pending` (it reports the tally, which still holds the application), not `off`.
- `axis_netpeer` only answers a call while it is woken by a line on stdin (fed by `tail -f` here, calls got "no answer" until a line arrived).
