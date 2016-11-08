#!/bin/bash

# Test Swarm cluster robustness

# @author Fabrice Jammes SLAC/IN2P3

set -x
set -e

DIR=$(cd "$(dirname "$0")"; pwd -P)
PARENT_DIR=$(dirname "$DIR")

. "$PARENT_DIR/env-infrastructure.sh"
SSH_CFG="$PARENT_DIR/ssh_config"

ssh -t -F "$SSH_CFG" "$MASTER" '{ sleep 1; sudo reboot -f; } >/dev/null &' 
while ! ssh -F "$SSH_CFG" "$MASTER" "docker ps" > /dev/null
do
    sleep 1
done
$DIR/run-query.sh

