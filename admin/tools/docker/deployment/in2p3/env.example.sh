# Rename this file to env.sh and edit configuration parameters
# env.sh is sourced by other scripts from the directory

# shmux path
PATH=$PATH:/opt/shmux/bin/

# Image names
# ===========

# Used to set images names, can be:
#   1. a git ticket branch but with _ instead of /
#   2. a git tag
# example: tickets_DM-5402
BRANCH=dev

# `docker run` settings
# =====================

# Data directory location on docker host, optional
HOST_DATA_DIR=/qserv/data

# Log directory location on docker host, optional
HOST_LOG_DIR=/qserv/log

# ulimit memory lock setting, in bytes, optional
ULIMIT_MEMLOCK=10737418240

# Nodes names
# ===========

# Format for all node names
HOSTNAME_FORMAT="ccqserv%g.in2p3.fr"

# Master id
MASTER_ID=100

# Workers range
WORKER_FIRST_ID=101
WORKER_LAST_ID=124

# Master id
# MASTER_ID=125

# Workers range
# WORKER_FIRST_ID=126
# WORKER_LAST_ID=149

# Advanced configuration
# ======================

DIR=$(cd "$(dirname "$0")"; pwd -P)
. "${DIR}/common.sh"
