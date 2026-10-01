# SPDX-License-Identifier: GPL-2.0
#
# Memory Bandwidth Analysis for Dynamic Bandwidth Adjustment
#
# Copyright (C) 2025, Technologies Co., Ltd.
# Author: Zeng Heng <zengheng4@huawei.com>

import json
import matplotlib.pyplot as plt
from draw_lib import parse_args, get_data

bw_max_limit = 140000

def get_lc_bw(data_list, idx):
    table = {}

    table['total_bw'] = []
    table['lc_bw'] = []

    for k, v in data_list[0]['lc_bw'].items():
        if k != "__grp_total__":
            table[k] = []

    for data in data_list:
        table['total_bw'].append(data['total_bw'][idx])
        table['lc_bw'].append(data['lc_bw']["__grp_total__"][idx])

        for k, v in data['lc_bw'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

    return table

def get_be_bw(data_list, idx):
    table = {}

    table['total_bw'] = []
    table['be_bw'] = []

    for k, v in data_list[0]['be_bw'].items():
        if k != "__grp_total__":
            table[k] = []

    for data in data_list:
        table['total_bw'].append(data['total_bw'][idx])
        table['be_bw'].append(data['be_bw']["__grp_total__"][idx])

        for k, v in data['be_bw'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

    return table

def get_all_bw(data_list, idx):
    table = {}

    table['total_bw'] = []
    table['lc_bw'] = []
    table['be_bw'] = []

    for k, v in data_list[0]['lc_bw'].items():
        if k != "__grp_total__":
            table[k] = []
    for k, v in data_list[0]['be_bw'].items():
        if k != "__grp_total__":
            table[k] = []

    for data in data_list:
        table['total_bw'].append(data['total_bw'][idx])
        table['lc_bw'].append(data['lc_bw']["__grp_total__"][idx])
        table['be_bw'].append(data['be_bw']["__grp_total__"][idx])

        for k, v in data['lc_bw'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

        for k, v in data['be_bw'].items():
            if k != "__grp_total__":
                table[k].append(v[idx])

    return table

def get_all_set(set_list, idx):
    setting = []

    for data in set_list:
        setting.append(data[idx]['setting'])

    return setting

def draw_data(bw_list, set_list, cpu_list, numa_th, percent):
    fig = plt.figure()
    x_arr = list(range(len(bw_list)))
    ref = [bw_max_limit * percent / 100] * len(bw_list)

    ax1 = fig.add_subplot(3, 1, 1)
    table = get_lc_bw(bw_list, numa_th)

    for k, v in table.items():
        ax1.plot(x_arr, v, label=k)
    ax1.plot(x_arr, ref, label='Reference')

    ax1.set_ylabel(f"LC Mem bandwidth (MB)")
    ax1.set_xlabel(f"Time (s)")
    ax1.legend()

    ax2 = fig.add_subplot(3, 1, 2)
    table = get_be_bw(bw_list, numa_th)

    for k, v in table.items():
        ax2.plot(x_arr, v, label=k)
    ax2.plot(x_arr, ref, label='Reference')

    ax2.set_ylabel(f"BE Mem bandwidth (MB)")
    ax2.set_xlabel(f"Time (s)")
    ax2.legend()

    ax3 = fig.add_subplot(3, 1, 3)
    setting = get_all_set(set_list, numa_th)
    ax3.plot(x_arr, setting, label='MBMAX')
    ax3.plot(x_arr, cpu_list, label='CPU Load')

    ax3.set_ylabel(f"Percent(%)")
    ax3.set_xlabel(f"Time (s)")
    ax3.legend()

    plt.tight_layout()
    plt.show()

    return

if __name__ == '__main__':
    start, percent = parse_args()
    if not start:
        start = 0
    if not percent:
        percent = 0

    filename = 'ctrlgrp_bw.data'
    bw_list = get_data(filename)
    filename = 'schemata_set.data'
    set_list = get_data(filename)
    filename = 'cpu_usage.data'
    cpu_list = get_data(filename)

    if not bw_list or not set_list:
        print('FileNotFound')

    if len(bw_list) != len(cpu_list):
        # length sync
        bw_list.pop()
        set_list.pop()

    draw_data(bw_list, set_list, cpu_list, start, percent)
