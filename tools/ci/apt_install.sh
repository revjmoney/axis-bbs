#!/bin/sh
# apt-get update and install with retries. GitHub runners sometimes cannot resolve the Ubuntu
# mirrors for a minute (DNS). apt-get update can exit 0 even then, so the install is retried too.
# Usage: sh tools/ci/apt_install.sh pkg1 pkg2 ...
set -u
tries=4
n=1
while :; do
    if sudo apt-get -o Acquire::Retries=3 update && \
       sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends "$@"; then
        exit 0
    fi
    if [ "$n" -ge "$tries" ]; then
        echo "apt failed after $tries tries" >&2
        exit 1
    fi
    echo "apt failed (try $n of $tries), retrying in $((n * 15)) s" >&2
    sleep $((n * 15))
    n=$((n + 1))
done
