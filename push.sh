#!/bin/bash

set -ex

TARGET_IP="${1:-192.168.33.38}"
TARGET_USER="root"
TARGET_PASS="admin"
REMOTE_BOOT="/boot/openeuler"

SSH_CMD="sshpass -p ${TARGET_PASS} ssh ${TARGET_USER}@${TARGET_IP}"
RSYNC_CMD="sshpass -p ${TARGET_PASS} rsync -avz"

${SSH_CMD} mkdir -p "${REMOTE_BOOT}"
${RSYNC_CMD} release/Image-6.* "${TARGET_USER}@${TARGET_IP}:${REMOTE_BOOT}/"
${RSYNC_CMD} release/rk3588-bdy-g98.dtb "${TARGET_USER}@${TARGET_IP}:${REMOTE_BOOT}/"
${RSYNC_CMD} -P --delete kos/lib/modules/6.* "${TARGET_USER}@${TARGET_IP}:/lib/modules/"
