# SPDX-License-Identifier: GPL-2.0
#
# CPU Load Sampling for Dynamic Bandwidth Adjustment
#
# Copyright (C) 2025, Technologies Co., Ltd.
# Author: Zeng Heng <zengheng4@huawei.com>

import time

def read_cpu_times():
    with open('/proc/stat', 'r') as f:
        line = f.readline()

    parts = line.split()
    return list(map(int, parts[1:]))

def calculate_cpu_utilization(times1, times2):
    total1 = sum(times1)
    idle1 = times1[3]
    total2 = sum(times2)
    idle2 = times2[3]

    total_diff = total2 - total1
    idle_diff = idle2 - idle1
    return (total_diff - idle_diff) * 100 / total_diff
