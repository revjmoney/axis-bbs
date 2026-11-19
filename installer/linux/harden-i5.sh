#!/usr/bin/env bash
# Hardens the Ubuntu box that hosts AXIS Native: firewall (ufw), key-only SSH,
# fail2ban for sshd, automatic security updates. Run with sudo:
#   sudo ./harden-i5.sh --dry-run     # print the plan and every command, change nothing (no sudo needed)
#   sudo ./harden-i5.sh               # do it (asks you to confirm the LAN subnet first)
#
# It is built so it cannot lock you out:
#   - It only adds firewall rules. It never resets ufw or deletes a rule.
#   - The SSH and Remote Desktop allow rules go in before the default becomes deny
#     and before ufw is enabled.
#   - The detected LAN subnet is shown and you must type y to go on. --yes does not skip that.
#   - SSH changes go in a drop-in file, are checked with sshd -t, and sshd is reloaded
#     (open sessions stay up). If the check fails, the drop-in is removed and the script stops.
#   - Key-only SSH is turned on only if your authorized_keys has a key in it.
set -euo pipefail

TAG="axis-harden"
SSHD_DROPIN="/etc/ssh/sshd_config.d/10-axis-hardening.conf"
F2B_JAIL="/etc/fail2ban/jail.d/axis-sshd.local"
AUTO_UPG="/etc/apt/apt.conf.d/20auto-upgrades"
AXIS_CONF="/var/lib/axis/axis.conf"
PANEL_PORT=2326
CLUSTER_PORT=2325
LINK_PORT=2324
SSH_PORT=22

dry=0
assume_yes=0
extra_rdp=()
extra_lan=()   # entries like 8080/tcp or 5353/udp
UFW_BEFORE="/root/axis-harden-ufw-before.txt"

usage() {
    echo "usage: sudo $0 [--dry-run] [--yes] [--rdp-port N] [--allow-lan-port N[/udp]]"
    echo "  --dry-run              print the plan and every command without running any (no sudo needed,"
    echo "                         but sudo lets it read axis.conf and the current ufw rules)"
    echo "  --yes                  skip the final 'apply?' question (the LAN subnet question is always asked)"
    echo "  --rdp-port N           also allow Remote Desktop on port N from the LAN (added to what is detected)"
    echo "  --allow-lan-port N     also allow port N (tcp, or N/udp) from the LAN; repeat for more"
}

is_port() { [[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -ge 1 ] && [ "$1" -le 65535 ]; }

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) dry=1; shift ;;
        --yes|-y) assume_yes=1; shift ;;
        --rdp-port) [ $# -ge 2 ] || { usage; exit 2; }
                    is_port "$2" || { echo "--rdp-port needs a port number (1-65535)" >&2; exit 2; }
                    extra_rdp+=("$2"); shift 2 ;;
        --allow-lan-port)
                    [ $# -ge 2 ] || { usage; exit 2; }
                    lp="${2%/*}"; lproto="tcp"
                    [ "$lp" != "$2" ] && lproto="${2#*/}"
                    is_port "$lp" || { echo "--allow-lan-port needs a port number (1-65535): $2" >&2; exit 2; }
                    [ "$lproto" = "tcp" ] || [ "$lproto" = "udp" ] || { echo "--allow-lan-port takes N, N/tcp or N/udp: $2" >&2; exit 2; }
                    extra_lan+=("$lp/$lproto"); shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 2 ;;
    esac
done

if [ "$dry" -eq 0 ] && [ "$(id -u)" -ne 0 ]; then
    echo "run this with sudo (or add --dry-run to only see the plan)" >&2
    exit 1
fi
is_root=0; [ "$(id -u)" -eq 0 ] && is_root=1

say()  { printf '%s\n' "$*"; }
head_() { printf '\n== %s ==\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*"; }

# Prints a command, then runs it unless this is a dry run.
run() {
    if [ "$dry" -eq 1 ]; then
        printf '  would run:'; printf ' %q' "$@"; printf '\n'
    else
        printf '  +'; printf ' %q' "$@"; printf '\n'
        "$@"
    fi
}

# Writes a file (mode 0644), showing its content. Skips it if the content is already the same.
write_file() {
    local path="$1" content="$2"
    if [ -r "$path" ] && [ "$(cat "$path")" = "$content" ]; then
        say "  $path already has this content, no change"
        return 1
    fi
    if [ "$dry" -eq 1 ]; then
        say "  would write $path:"
    else
        say "  + write $path:"
        printf '%s\n' "$content" > "$path"
        chmod 0644 "$path"
    fi
    printf '%s\n' "$content" | sed 's/^/      | /'
    return 0
}

ip4_to_int() {
    local a b c d
    IFS=. read -r a b c d <<<"$1"
    echo $(( (a << 24) | (b << 16) | (c << 8) | d ))
}

# in_subnet 10.1.2.3 10.1.2.0/24
in_subnet() {
    local ip="$1" net="${2%/*}" bits="${2#*/}" mask
    mask=$(( bits == 0 ? 0 : (0xFFFFFFFF << (32 - bits)) & 0xFFFFFFFF ))
    [ $(( $(ip4_to_int "$ip") & mask )) -eq $(( $(ip4_to_int "$net") & mask )) ]
}

# ---------------------------------------------------------------- detection (no changes)

head_ "Network"
iface="$(ip -4 route show default 2>/dev/null | awk '{for(i=1;i<NF;i++) if($i=="dev"){print $(i+1); exit}}')"
if [ -z "$iface" ]; then
    echo "no IPv4 default route found, so the LAN subnet cannot be worked out. Stopping." >&2
    exit 1
fi
subnets="$(ip -4 route show dev "$iface" proto kernel scope link 2>/dev/null | awk '$1 ~ /\// {print $1}' | sort -u)"
if [ "$(printf '%s\n' "$subnets" | grep -c .)" -gt 1 ]; then
    echo "$iface has more than one LAN subnet ($(echo $subnets)); not sure which is the LAN. Stopping." >&2
    exit 1
fi
subnet="$subnets"
if [ -z "$subnet" ] || ! [[ "$subnet" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/([0-9]+)$ ]]; then
    echo "could not find the LAN subnet on $iface. Stopping." >&2
    exit 1
fi
bits="${BASH_REMATCH[1]}"
if [ "$bits" -lt 8 ] || [ "$bits" -gt 30 ]; then
    echo "the LAN subnet on $iface is $subnet, which does not look like a home LAN. Stopping." >&2
    exit 1
fi
say "Default route interface: $iface"
say "LAN subnet (IPv4):       $subnet"

# IPv6 on the same LAN: link-local plus any on-link prefixes, so a hostname that
# resolves to IPv6 still reaches SSH and Remote Desktop.
lan6=()
ufw_ipv6="$(sed -n 's/^IPV6=//p' /etc/default/ufw 2>/dev/null | tr -d '"' || true)"
if [ "$ufw_ipv6" = "yes" ] && ip -6 addr show dev "$iface" 2>/dev/null | grep -q 'inet6 fe80:'; then
    lan6+=("fe80::/10")
    while read -r p; do
        [ -n "$p" ] && [[ "$p" != fe80:* ]] && lan6+=("$p")
    done < <(ip -6 route show dev "$iface" 2>/dev/null | awk '$1 ~ /\// && $1 !~ /^default/ && !/ via / {print $1}' | sort -u)
fi
if [ ${#lan6[@]} -gt 0 ]; then
    say "LAN prefixes (IPv6):     ${lan6[*]}"
else
    say "LAN prefixes (IPv6):     none (IPv6 is not covered by the LAN rules)"
fi
lan_srcs=("$subnet" "${lan6[@]}")

vpn_ifaces=()
while read -r ifn; do
    case "$ifn" in tailscale*|wg*|zt*|tun*) vpn_ifaces+=("$ifn") ;; esac
done < <(ip -o link show 2>/dev/null | awk -F': ' '{sub(/@.*/, "", $2); print $2}')
# Address ranges reached over the VPN (for fail2ban's ignore list).
vpn_nets=()
for ifn in "${vpn_ifaces[@]}"; do
    case "$ifn" in tailscale*) vpn_nets+=("100.64.0.0/10" "fd7a:115c:a1e0::/48") ;; esac
    while read -r n; do
        [ -n "$n" ] && vpn_nets+=("$n")
    done < <( { ip -4 route show table all dev "$ifn" 2>/dev/null; ip -6 route show table all dev "$ifn" 2>/dev/null; } \
              | awk '$1 ~ /\// && $1 !~ /^(default|fe80|ff00|multicast|local|broadcast)/ {print $1}')
done
if [ ${#vpn_nets[@]} -gt 0 ]; then
    mapfile -t vpn_nets < <(printf '%s\n' "${vpn_nets[@]}" | sort -u)
fi
if [ ${#vpn_ifaces[@]} -gt 0 ]; then
    say "VPN interfaces:          ${vpn_ifaces[*]} (SSH and Remote Desktop will be allowed on these too)"
    say "VPN ranges:              ${vpn_nets[*]:-none seen}"
else
    say "VPN interfaces:          none found (tailscale*, wg*, zt*, tun*)"
fi

# Where this session comes from, if it can be seen. Abort if it is outside the LAN and no VPN.
# sudo usually drops SSH_CONNECTION, so look for it in the parent processes too.
client=""
if [ -n "${SSH_CONNECTION:-}" ]; then
    client="${SSH_CONNECTION%% *}"
else
    pid=$PPID
    while [ -n "$pid" ] && [ "$pid" -gt 1 ] && [ -z "$client" ]; do
        sc="$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | sed -n 's/^SSH_CONNECTION=//p' | head -n1 || true)"
        [ -n "$sc" ] && client="${sc%% *}"
        pid="$(awk '/^PPid:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)"
    done
fi
if [ -z "$client" ]; then
    client="$(who -m 2>/dev/null | sed -n 's/.*(\(.*\)).*/\1/p' || true)"
fi
if [ -n "$client" ] && [[ "$client" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if in_subnet "$client" "$subnet"; then
        say "This session comes from: inside the LAN subnet (good)"
    elif [ ${#vpn_ifaces[@]} -gt 0 ]; then
        warn "this session comes from outside $subnet; make sure it is over ${vpn_ifaces[*]}"
    else
        echo "This session comes from outside $subnet and there is no VPN interface." >&2
        echo "The new rules would cut it off. Run this from the LAN. Stopping." >&2
        exit 1
    fi
elif [ -n "$client" ]; then
    say "This session comes from: $client (not IPv4, not checked)"
else
    say "This session comes from: not an SSH session (or not visible), not checked"
fi

head_ "Remote Desktop"
rdp_ports=()
rdp_note=""
listening_tcp="$(ss -tlnH 2>/dev/null | awk '{print $4}')"
port_listening() { printf '%s\n' "$listening_tcp" | grep -qE "[:.]$1\$"; }
if systemctl is-active --quiet xrdp 2>/dev/null; then
    p="$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*//p' /etc/xrdp/xrdp.ini 2>/dev/null | head -n1 | grep -oE '[0-9]+$' || true)"
    is_port "${p:-}" || p=3389
    rdp_ports+=("$p"); rdp_note="xrdp is active (port $p from /etc/xrdp/xrdp.ini or its default)"
fi
grd_sys=0; grd_user=0
systemctl is-active --quiet gnome-remote-desktop 2>/dev/null && grd_sys=1
if [ -n "${SUDO_USER:-}" ]; then
    sudo_uid="$(id -u "$SUDO_USER" 2>/dev/null || true)"
    if [ -n "$sudo_uid" ] && [ -S "/run/user/$sudo_uid/bus" ]; then
        if [ "$is_root" -eq 1 ]; then
            runuser -u "$SUDO_USER" -- env XDG_RUNTIME_DIR="/run/user/$sudo_uid" \
                systemctl --user is-active --quiet gnome-remote-desktop 2>/dev/null && grd_user=1
        fi
    fi
else
    systemctl --user is-active --quiet gnome-remote-desktop 2>/dev/null && grd_user=1
fi
if [ "$grd_sys" -eq 1 ] || [ "$grd_user" -eq 1 ]; then
    found=()
    # Ports gnome-remote-desktop is listening on (process names show only as root).
    if [ "$is_root" -eq 1 ]; then
        while read -r p; do found+=("$p"); done < <(ss -tlnpH 2>/dev/null | awk '/gnome-remote-de/ {n=split($4,a,":"); print a[n]}' | sort -u)
    fi
    # The system config, when readable.
    p="$(sed -n '/^\[RDP\]/,/^\[/{s/^port=//p}' /etc/gnome-remote-desktop/grd.conf 2>/dev/null | head -n1 || true)"
    is_port "${p:-}" && found+=("$p")
    if [ ${#found[@]} -eq 0 ] && port_listening 3389; then found+=(3389); fi
    if [ ${#found[@]} -eq 0 ]; then found+=(3389); gnote=" (port not seen, using 3389)"; else gnote=""; fi
    which=""
    [ "$grd_sys" -eq 1 ] && which="system"
    [ "$grd_user" -eq 1 ] && which="${which:+$which and }user"
    rdp_ports+=("${found[@]}")
    rdp_note="${rdp_note:+$rdp_note; }GNOME Remote Desktop ($which) is active$gnote"
fi
if [ ${#rdp_ports[@]} -eq 0 ]; then
    rdp_ports+=(3389)
    rdp_note="no xrdp or GNOME Remote Desktop seen active; allowing 3389 anyway so RDP keeps working"
fi
rdp_ports+=("${extra_rdp[@]}")
mapfile -t rdp_ports < <(printf '%s\n' "${rdp_ports[@]}" | sort -un)
say "$rdp_note"
say "Remote Desktop port(s):  ${rdp_ports[*]}"
for p in "${rdp_ports[@]}"; do
    port_listening "$p" && say "  port $p is listening now" || warn "nothing is listening on $p right now"
done

head_ "AXIS ports"
lan_port=2323
public_port=""
# conf_value KEY: the last KEY=value in axis.conf, without a trailing "# comment" or quotes.
conf_value() {
    sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$AXIS_CONF" | tail -n1 \
        | sed 's/[[:space:]]*#.*$//' | tr -d "[:space:]\"'"
}
if [ -r "$AXIS_CONF" ]; then
    v="$(conf_value lan_port)"
    if [ -n "$v" ]; then
        if is_port "$v"; then lan_port="$v"; else warn "lan_port '$v' in $AXIS_CONF is not a port, using 2323"; fi
    fi
    v="$(conf_value public_port)"
    if [ -z "$v" ] || [ "$v" = "0" ]; then
        say "public_port is off in $AXIS_CONF, no public rule"
    elif is_port "$v"; then
        public_port="$v"
    else
        warn "public_port '$v' in $AXIS_CONF is not a port, no public rule"
    fi
elif [ "$is_root" -eq 0 ] && { [ -e "$AXIS_CONF" ] || { [ -d "$(dirname "$AXIS_CONF")" ] && ! [ -x "$(dirname "$AXIS_CONF")" ]; }; }; then
    say "$AXIS_CONF cannot be read without sudo; the real run reads it. This plan assumes"
    say "lan_port 2323 and no public port, so it may show the wrong LAN port and leave out a"
    say "public rule. For the exact plan: sudo ./harden-i5.sh --dry-run"
elif [ -e "$AXIS_CONF" ]; then
    say "$AXIS_CONF is not readable, so lan_port is taken as 2323 and the public rule is skipped"
else
    say "$AXIS_CONF does not exist yet, so lan_port is taken as 2323 and the public rule is skipped"
fi
say "LAN BBS port:            $lan_port/tcp (from the LAN)"
say "Panel port:              $PANEL_PORT/tcp (from the LAN)"
say "Cluster port:            $CLUSTER_PORT/udp (from the LAN)"
say "Cluster link port:       $LINK_PORT/tcp (from the LAN)"
if [ -n "$public_port" ]; then
    say "Public BBS port:         $public_port/tcp (from anywhere)"
fi
if [ ${#extra_lan[@]} -gt 0 ]; then
    say "Extra LAN ports:         ${extra_lan[*]} (from the LAN, --allow-lan-port)"
fi

head_ "Firewall now"
ufwv=""
ufw_ok=1
command -v ufw >/dev/null 2>&1 || ufw_ok=0
if [ "$ufw_ok" -eq 0 ]; then
    say "ufw is not installed; it will be installed"
elif [ "$is_root" -eq 1 ]; then
    ufwv="$(ufw status verbose 2>&1 || true)"
    printf '%s\n' "$ufwv"
    if grep -q '^Status: active' <<<"$ufwv" && grep -q 'deny (incoming)' <<<"$ufwv"; then
        say "Incoming is already deny by default, no change there."
    else
        say "Incoming is not deny yet: it will become deny AFTER the allow rules below are in."
    fi
    # Existing rules that open SSH or Remote Desktop to everyone (this script will not delete them).
    broad="$(grep -E "^($SSH_PORT|OpenSSH|$(IFS='|'; echo "${rdp_ports[*]}"))(/tcp)?([[:space:]]|\(v6\))" <<<"$ufwv" | grep -E 'Anywhere[[:space:]]*$|Anywhere \(v6\)[[:space:]]*$' | grep -v ' on ' || true)"
    if [ -n "$broad" ]; then
        warn "these existing rules open SSH or Remote Desktop to any address, not just the LAN:"
        printf '%s\n' "$broad" | sed 's/^/    /'
        say "  This script leaves them alone. Once the LAN rules work, you can remove them with"
        say "  'sudo ufw status numbered' and then 'sudo ufw delete N' (numbers shift after each"
        say "  delete, so list again before the next one)."
    fi
else
    say "ufw is installed and its rules and default policy cannot be read without sudo."
    say "The real run prints them here first and saves them to $UFW_BEFORE."
    say "It only adds rules; it never deletes or resets. For the exact plan: sudo ./harden-i5.sh --dry-run"
fi

# Things listening on the network that this script adds no rule for (they get blocked
# from outside once incoming is deny, unless an existing rule already opens them).
allowed=("$SSH_PORT/tcp" "$lan_port/tcp" "$PANEL_PORT/tcp" "$CLUSTER_PORT/udp" "$LINK_PORT/tcp" "${extra_lan[@]}")
for p in "${rdp_ports[@]}"; do allowed+=("$p/tcp"); done
[ -n "$public_port" ] && allowed+=("$public_port/tcp")
others=""
other_cmds=""
seen=" "
while read -r proto addr; do
    case "$addr" in 127.*|"[::1]"*|*%lo:*) continue ;; esac
    port="${addr##*:}"
    is_port "$port" || continue
    [[ " ${allowed[*]} " == *" $port/$proto "* ]] && continue
    others+="    $proto $addr"$'\n'
    [[ "$seen" == *" $port/$proto "* ]] && continue
    seen+="$port/$proto "
    for src in "${lan_srcs[@]}"; do
        other_cmds+="    sudo ufw allow from $src to any port $port proto $proto comment '$TAG lan $port'"$'\n'
    done
done < <(ss -tulnH 2>/dev/null | awk '{print $1, $5}' | sort -u)
if [ -n "$others" ]; then
    warn "these are listening on the network and get no new rule (blocked from outside unless a rule already allows them):"
    printf '%s' "$others"
    say "  To keep them open to the LAN, rerun with --allow-lan-port PORT (or PORT/udp), or run:"
    printf '%s' "$other_cmds"
fi

head_ "SSH"
key_user="${SUDO_USER:-}"
[ -z "$key_user" ] && [ "$dry" -eq 1 ] && key_user="$(id -un)"
keyonly=0
if [ -z "$key_user" ] || [ "$key_user" = "root" ]; then
    say "Not run through sudo by a normal user, so there is no authorized_keys to check."
    say "Skipping key-only SSH (passwords stay allowed)."
else
    key_home="$(getent passwd "$key_user" | cut -d: -f6)"
    ak="$key_home/.ssh/authorized_keys"
    nkeys=0
    if [ -r "$ak" ]; then
        nkeys="$(grep -cE '(^|[[:space:]])(ssh-(rsa|ed25519|dss)|ecdsa-sha2-[^[:space:]]+|sk-[^[:space:]]+)[[:space:]]+AAAA' "$ak" || true)"
    fi
    if [ "${nkeys:-0}" -ge 1 ]; then
        keyonly=1
        say "$ak has $nkeys key(s): SSH becomes key-only (no passwords, no root login)."
    else
        say "$ak has no key (or is missing). Skipping key-only SSH, so you are not locked out."
        say "Add your public key there, then run this again to turn it on."
    fi
fi
others_dropin="$(ls /etc/ssh/sshd_config.d/ 2>/dev/null | grep -v "^$(basename "$SSHD_DROPIN")\$" || true)"
[ -n "$others_dropin" ] && say "Other sshd drop-ins (read in name order, first value wins): $others_dropin"

# ---------------------------------------------------------------- confirm

head_ "Confirm"
say "SSH ($SSH_PORT) and Remote Desktop (${rdp_ports[*]}) will be allowed only from:"
say "  $subnet${lan6:+ and ${lan6[*]}}${vpn_ifaces:+, and on ${vpn_ifaces[*]}}"
if [ "$dry" -eq 1 ]; then
    say "(dry run: the real run asks 'Is $subnet your LAN? [y/N]' here and stops on anything but y)"
else
    if ! { exec 3</dev/tty; } 2>/dev/null; then
        echo "no terminal to ask on, so the LAN subnet cannot be confirmed. Stopping." >&2
        exit 1
    fi
    printf 'Is %s your LAN, the network you reach this box from? [y/N] ' "$subnet"
    read -r ans <&3 || ans=""
    [ "$ans" = "y" ] || [ "$ans" = "Y" ] || { say "Stopped. Nothing was changed."; exit 1; }
    if [ "$assume_yes" -eq 0 ]; then
        printf 'Apply the changes above? [y/N] '
        read -r ans <&3 || ans=""
        [ "$ans" = "y" ] || [ "$ans" = "Y" ] || { say "Stopped. Nothing was changed."; exit 1; }
    fi
    exec 3<&-
fi

# ---------------------------------------------------------------- changes

export DEBIAN_FRONTEND=noninteractive
need_pkgs=()
[ "$ufw_ok" -eq 0 ] && need_pkgs+=(ufw)
command -v fail2ban-client >/dev/null 2>&1 || need_pkgs+=(fail2ban)
dpkg -s unattended-upgrades >/dev/null 2>&1 || need_pkgs+=(unattended-upgrades)
head_ "Packages"
if [ ${#need_pkgs[@]} -gt 0 ]; then
    run apt-get update
    run apt-get install -y "${need_pkgs[@]}"
else
    say "ufw, fail2ban and unattended-upgrades are already installed"
fi

head_ "SSH: key-only"
if [ "$keyonly" -eq 1 ]; then
    content="# Added by harden-i5.sh (AXIS). Remove this file and reload ssh to undo.
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no"
    if write_file "$SSHD_DROPIN" "$content"; then
        if [ "$dry" -eq 1 ]; then
            run sshd -t
            say "  would reload ssh only if the check passes (if it fails: remove the drop-in and stop)"
            run systemctl reload ssh
        else
            [ -d /run/sshd ] || run mkdir -p /run/sshd
            say "  + sshd -t"
            if ! sshd -t; then
                rm -f "$SSHD_DROPIN"
                echo "sshd -t failed with the new drop-in. Removed $SSHD_DROPIN; ssh was not reloaded. Stopping." >&2
                exit 1
            fi
            if systemctl is-active --quiet ssh; then
                run systemctl reload ssh
            elif systemctl is-active --quiet sshd; then
                run systemctl reload sshd
            else
                say "  ssh.service is not running (socket activated); new connections read the new settings"
            fi
        fi
    fi
    if [ "$dry" -eq 0 ]; then
        say "  Effective settings now:"
        eff="$(sshd -T 2>/dev/null || true)"
        printf '%s\n' "$eff" | grep -Ei '^(passwordauthentication|kbdinteractiveauthentication|permitrootlogin) ' | sed 's/^/    /' || true
        if ! printf '%s\n' "$eff" | grep -qi '^passwordauthentication no'; then
            warn "PasswordAuthentication is still on; an earlier drop-in or sshd_config sets it first"
        fi
    fi
else
    say "  skipped (see above)"
fi

head_ "Firewall"
# Keep the state from before the first run, so the default policy can be put back.
if [ -e "$UFW_BEFORE" ]; then
    say "  $UFW_BEFORE already exists (from an earlier run), kept as is"
elif [ "$dry" -eq 1 ]; then
    say "  would save the current 'ufw status verbose' to $UFW_BEFORE"
else
    say "  + save the current 'ufw status verbose' to $UFW_BEFORE"
    { echo "# ufw before harden-i5.sh, $(date)"; printf '%s\n' "${ufwv:-ufw was not installed}"; } > "$UFW_BEFORE"
    chmod 0600 "$UFW_BEFORE"
fi
for src in "${lan_srcs[@]}"; do
    run ufw allow from "$src" to any port "$SSH_PORT" proto tcp comment "$TAG ssh"
    for p in "${rdp_ports[@]}"; do
        run ufw allow from "$src" to any port "$p" proto tcp comment "$TAG rdp"
    done
done
for ifn in "${vpn_ifaces[@]}"; do
    run ufw allow in on "$ifn" to any port "$SSH_PORT" proto tcp comment "$TAG ssh vpn"
    for p in "${rdp_ports[@]}"; do
        run ufw allow in on "$ifn" to any port "$p" proto tcp comment "$TAG rdp vpn"
    done
done
for src in "${lan_srcs[@]}"; do
    run ufw allow from "$src" to any port "$lan_port" proto tcp comment "$TAG bbs lan"
    run ufw allow from "$src" to any port "$PANEL_PORT" proto tcp comment "$TAG panel"
    run ufw allow from "$src" to any port "$CLUSTER_PORT" proto udp comment "$TAG cluster"
    run ufw allow from "$src" to any port "$LINK_PORT" proto tcp comment "$TAG cluster link"
    for e in "${extra_lan[@]}"; do
        run ufw allow from "$src" to any port "${e%/*}" proto "${e#*/}" comment "$TAG lan ${e%/*}"
    done
done
if [ -n "$public_port" ]; then
    run ufw allow "$public_port/tcp" comment "$TAG bbs public"
fi
run ufw default allow outgoing
run ufw default deny incoming
run ufw --force enable
[ "$dry" -eq 0 ] && ufw status verbose

head_ "fail2ban: sshd jail"
# SSH is reachable only from the LAN (and VPN) after this, so never ban those: that would be you.
ignore="127.0.0.1/8 ::1 ${lan_srcs[*]}${vpn_nets[*]:+ ${vpn_nets[*]}}"
jail="# Added by harden-i5.sh (AXIS).
[sshd]
enabled = true
backend = systemd
ignoreip = $ignore
bantime = 1h
maxretry = 5"
[ "$dry" -eq 0 ] && mkdir -p "$(dirname "$F2B_JAIL")"
write_file "$F2B_JAIL" "$jail" || true
run systemctl enable fail2ban || warn "could not enable fail2ban"
run systemctl restart fail2ban || warn "fail2ban did not restart; see 'journalctl -u fail2ban'"
if [ "$dry" -eq 0 ]; then
    sleep 2
    fail2ban-client status sshd || warn "the sshd jail did not come up; see 'journalctl -u fail2ban'. The rest is done."
fi

head_ "Automatic security updates"
upg='APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";'
if [ -r "$AUTO_UPG" ] && grep -q 'Update-Package-Lists "1"' "$AUTO_UPG" && grep -q 'Unattended-Upgrade "1"' "$AUTO_UPG"; then
    say "  $AUTO_UPG already turns them on, no change"
else
    write_file "$AUTO_UPG" "$upg" || true
fi
run systemctl enable --now unattended-upgrades || warn "could not enable unattended-upgrades"
run systemctl enable --now apt-daily.timer apt-daily-upgrade.timer || warn "could not enable the apt timers"

head_ "Listening services (ss -tulpn)"
if [ "$is_root" -eq 1 ]; then ss -tulpn; else ss -tuln; say "(process names show only with sudo)"; fi

head_ "Done"
if [ "$dry" -eq 1 ]; then
    say "Dry run: nothing was changed."
else
    say "Hardening applied. Before closing this session, open a NEW ssh session and a Remote"
    say "Desktop connection from your PC to check both still get in."
fi
