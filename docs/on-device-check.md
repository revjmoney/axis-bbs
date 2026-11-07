# On-device check (node 1)

Host tests run the native LORD engine, the PC DOS emulator (`doorhost`), and ZMODEM. They do not boot the ESP32, and they do not
reproduce the CYD's byte-heap fragmentation. This is the short check to run on the board itself.
Do not point it at someone else's BBS. Use a board you can flash and a serial console at 115200.

The guest cap stays 128 KB. The build stays no-PSRAM, with the 4-bit screen copy.

## Before a caller

1. Flash `pio run -e cyd` (or `headless`) with a full upload after a partition change.
2. On the serial console, run `@STATUS`.
3. Write down `heap`, `largest`, `min`, `stack`, `reset`, `door`, and `door_peak`.
   `reset=panic`, `reset=wdt`, or `reset=brownout` means the last boot was not a clean power-on.

## LORD, then hang up

1. Telnet to this board only. Log in on node 1 and open LORD.
2. The serial line `axis: door lord ... byte heap ... lent from the screen ... instruction RAM ... stack`
   is the start snapshot. `door=running` on a second `@STATUS`.
3. Play as far as the forest fight if you are measuring the old crash, then quit back to the menu.
   A shorter pass (title, warrior list, quit) still checks launch and cleanup.
4. The serial line `axis: door lord done` gives `peak RAM`, heap before and after, lowest heap, and
   stack words. `@STATUS` should show `door=quit` or `door=oom` and a `door_peak` in KB.
   `stack` or `door_stack` under 128 words is a stack warning, not a pass.
5. Open LORD again and hang the telnet client up from inside it. The board should return to the
   waiting screen without rebooting. `@STATUS` then: `door=hangup`, and `heap` / `largest` back in
   the same range as step 2 of "Before a caller" (a few KB of drift is normal; a collapse is not).

## Transfer while another node is up

1. With node 1 at a menu, start a second caller if you have a second session (this board's other
   node, or a headless node you flashed).
2. ZMODEM a file larger than 100 KB down, then the same file up into a test area.
3. Compare the bytes (size and a checksum) with the original. A mid-transfer disconnect must not
   leave that upload listed in the area.
4. `@STATUS` again. `min` is the lowest byte heap since boot, so it will not climb back up.

`@STATUS` is the serial console and the panel, and the sysop menu's Status entry. It does not
print passwords, hashes, or webhook URLs.

A passing host test plus a passing run of this list is still not proof that a particular crash
on a particular board is gone. Keep the serial log from a failure (`heap` / `largest` / `cap` or
`heap` on the out-of-memory line, and `reset` after the reboot).
