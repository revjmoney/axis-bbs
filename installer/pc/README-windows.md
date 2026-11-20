# axispc on Windows (portable)

`axispc.exe` is the AXIS BBS built as a native Windows x64 program (the same shared core the
ESP32 board and the Linux build run), on one event loop thread. This zip is the program, the
ANSI art from the source tree, and this file. It is not an installer and it does not register
a Windows service.

Unzip it anywhere and run it from a console (Windows Terminal, or the Windows console). It
refuses to run elevated: use an ordinary user.

    axispc.exe --terminal --data %LOCALAPPDATA%\axis
    axispc.exe --headless --data %LOCALAPPDATA%\axis

`--terminal` is the default when stdin and stdout are a console: the sysop's waiting-for-caller
screen. `--headless` leaves the log on the console. With no `--data`, the data folder is
`%LOCALAPPDATA%\axis`, or `%USERPROFILE%\.axis` when `LOCALAPPDATA` is unset.

The folder layout matches the Linux build, so a copy of a Linux data folder works as it is:

* `<data>\sd` : menus, messages, files
* `<data>\flash` : `axis.ini` and `users.dat`
* `<data>\prefs`
* `<data>\log` : `axispc.log` (terminal mode writes the log here)
* `<data>\axis.conf` : ports and limits (`lan_port` default 2323, `public_port` default 0)

`--seed CARD_FOLDER` copies a board card into an empty `<data>\sd` on the first run. The `art`
folder in this zip is the ANSI art from the source tree (menus and waiting screens). The
program already has its built-in waiting screen; callers still want a terminal such as
SyncTERM (80x25, CP437), not the Windows `telnet` client.

Ctrl-C stops it: callers are hung up, the users and the stats are saved. A second Ctrl-C stops
at once. Closing the console window asks for the same stop and gives it a few seconds.

`--modem COM3` is accepted and ignored. The LFD3K-SM radio line is not opened on Windows yet.

Once a day, while `update_check` is on (the default), axispc asks GitHub whether a newer
`pc-v*` release exists. That uses WinHTTP, the system certificate store and the system proxy.
`update_check=0` in `axis.conf` turns it off. The check does not download or install anything.

Terminal mode needs virtual-terminal processing (Windows 10 and later, Windows Terminal). If
the console cannot do it, axispc says so and runs headless. The data folder is not given Unix
mode 0700; it keeps your user's normal permissions.
