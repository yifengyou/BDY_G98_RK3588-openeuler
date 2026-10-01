# SPDX-License-Identifier: GPL-2.0
#
# CPU Load Analysis for Dynamic Bandwidth Adjustment
#
# Copyright (C) 2025, Technologies Co., Ltd.
# Author: Zeng Heng <zengheng4@huawei.com>

import json
import matplotlib.pyplot as plt

def draw_data(cpu_list):
    fig = plt.figure()
    x_arr = list(range(len(cpu_list)))

    ax1 = fig.add_subplot(1, 1, 1)

    ax1.plot(x_arr, cpu_list, label='CPU usage')
    ax1.legend()

    plt.tight_layout()
    plt.show()

    return

def get_data(file):
    try:
        with open(file, 'r') as f:
            cpu_list = json.load(f)
    except FileNotFoundError:
        return None

    return cpu_list

if __name__ == '__main__':
    filename = 'cpu_usage.data'
    cpu_list = get_data(filename)

    if cpu_list:
        draw_data(cpu_list)
    else:
        print('FileNotFound')
