#!/bin/bash

set -ex

cd release

sshpass -p admin rsync -avz Image-6.6.0-kdev 192.168.33.38:/boot/
sshpass -p admin rsync -avz rk3588-bdy-g98.dtb 192.168.33.38:/boot/openeuler/




