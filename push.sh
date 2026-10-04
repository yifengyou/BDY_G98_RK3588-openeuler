#!/bin/bash

set -ex

sshpass -p admin rsync -avz release/Image-6.* 192.168.33.38:/boot/openeuler/
sshpass -p admin rsync -avz release/rk3588-bdy-g98.dtb 192.168.33.38:/boot/openeuler/
sshpass -p admin rsync -avz -P --delete kos/lib/modules/* 192.168.33.38:/lib/modules/

