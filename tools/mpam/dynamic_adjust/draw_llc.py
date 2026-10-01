# SPDX-License-Identifier: GPL-2.0
#
# Cache Storage Analysis for Dynamic Bandwidth Adjustment
#
# Copyright (C) 2025, Technologies Co., Ltd.
# Author: Zeng Heng <zengheng4@huawei.com>

import matplotlib.pyplot as plt
from draw_lib import parse_args, get_data

def get_lc_llc(data_list, idx):
    table = {}

    table['lc_llc'] = []

    for k, v in data_list[0]['lc_llc'].items():
        if k != "__grp_total__":
            table[k] = []

    for data in data_list:
        table['lc_llc'].append(data['lc_llc']["__grp_total__"][idx])
        for k, v in data['lc_llc'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

    return table

def get_be_llc(data_list, idx):
    table = {}

    table['be_llc'] = []

    for k, v in data_list[0]['be_llc'].items():
        if k != "__grp_total__":
            table[k] = []

    for data in data_list:
        table['be_llc'].append(data['be_llc']["__grp_total__"][idx])
        for k, v in data['be_llc'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

    return table

def get_all_llc(data_list, idx):
    table = {}

    table['lc_llc'] = []
    table['be_llc'] = []

    for k, v in data_list[0]['lc_llc'].items():
        if k != "__grp_total__":
            table[k] = []
    for k, v in data_list[0]['be_llc'].items():
        if k != "__grp_total__":
            table[k] = []

    for data in data_list:
        table['lc_llc'].append(data['lc_llc']["__grp_total__"][idx])
        table['be_llc'].append(data['be_llc']["__grp_total__"][idx])

        for k, v in data['lc_llc'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

        for k, v in data['be_llc'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

    return table

def draw_data(llc_list, cpu_list, numa_th):
    fig = plt.figure()
    x_arr = list(range(len(llc_list)))

    ax1 = fig.add_subplot(3, 1, 1)
    table = get_lc_llc(llc_list, numa_th)

    for k, v in table.items():
        ax1.plot(x_arr, v, label=k)

    ax1.set_ylabel(f"LC L3 Cache (KB)")
    ax1.set_xlabel(f"Time (s)")
    ax1.legend()

    ax2 = fig.add_subplot(3, 1, 2)
    table = get_be_llc(llc_list, numa_th)

    for k, v in table.items():
        ax2.plot(x_arr, v, label=k)

    ax2.set_ylabel(f"BE L3 Cache (KB)")
    ax2.set_xlabel(f"Time (s)")
    ax2.legend()

    ax3 = fig.add_subplot(3, 1, 3)
    ax3.plot(x_arr, cpu_list, label='CPU usage')

    ax3.set_ylabel(f"CPU Load (%)")
    ax3.set_xlabel(f"Time (s)")

    ax3.legend()

    plt.tight_layout()
    plt.show()

    return

if __name__ == '__main__':
    start, p = parse_args()
    if not start:
        start = 0

    filename = 'ctrlgrp_llc.data'
    llc_list = get_data(filename)
    filename = 'cpu_usage.data'
    cpu_list = get_data(filename)

    if not llc_list:
        print('FileNotFound')

    if len(llc_list) != len(cpu_list):
        # length sync
        llc_list.pop()

    draw_data(llc_list, cpu_list, start)
