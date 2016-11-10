#!/bin/bash

# Test Swarm cluster robustness

# @author Fabrice Jammes SLAC/IN2P3

set -x
set -e

DIR=$(cd "$(dirname "$0")"; pwd -P)
PARENT_DIR=$(dirname "$DIR")

. "$PARENT_DIR/env-infrastructure.sh"
SSH_CFG="$PARENT_DIR/ssh_config"

i=1
for node in $WORKERS
do
    ssh -t -F "$SSH_CFG" "$node" 'sudo -b sh -c "sleep 1; init 6"'
    while ssh -F "$SSH_CFG" "$node" "~/wait.sh"
    do
        echo "Waiting for $node to shutdown"
        sleep 1
    done
    while ! ssh -F "$SSH_CFG" "$node" "~/wait.sh"
    do
        echo "Waiting for Qserv to start on $node"
        sleep 1
    done
    start=`date +%s`
    while ! ssh -F "$SSH_CFG" "$MASTER" 'CONTAINER_ID=$(docker ps -l -q); \
        docker exec "$CONTAINER_ID" curl --connect-timeout 1 -s worker-'${i}':5012'
    do
        end=`date +%s`
        duration=$((end-start))
        echo "Waiting on master for worker-${i}:5012 availability since $duration ms"
        sleep 1
    done
    $DIR/run-query.sh
    i=$((i+1))
done
