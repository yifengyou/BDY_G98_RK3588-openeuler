#!/bin/bash

set -ex

sshpass -p admin rsync -avz release/Image-6.6.0-kdev 192.168.33.38:/boot/
sshpass -p admin rsync -avz release/rk3588-bdy-g98.dtb 192.168.33.38:/boot/openeuler/




