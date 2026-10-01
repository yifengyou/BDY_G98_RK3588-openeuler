# SPDX-License-Identifier: GPL-2.0
#
# Dynamic Bandwidth Adjustment
#
# Copyright (C) 2025, Technologies Co., Ltd.
# Author: Zeng Heng <zengheng4@huawei.com>

import os, time, signal, json, sys, glob
import argparse
import subprocess
from pid_controller import PID_Controller
from calc_cpu_usage import read_cpu_times, calculate_cpu_utilization

__version__ = "1.0.0"

numa_bw_limit = 140000

bw_list = []
llc_list = []
set_list = []
cpu_list = []

def read_bw(grp):
    grps_val = {}

    for g in grp:
        vals = []

        resctrl_mon_data_dir = '/sys/fs/resctrl/%s/mon_data/' % g
        mon_data_dirs = os.listdir(resctrl_mon_data_dir)
        mon_MB_dirs = [dir for dir in mon_data_dirs if dir.startswith('mon_MB_')]
        mon_MB_dirs.sort()

        for mb_dir in mon_MB_dirs:
            with open(resctrl_mon_data_dir + mb_dir + '/mbm_total_bytes', 'r') as f:
            # with open(resctrl_mon_data_dir + mb_dir, 'r') as f:
                line = f.readline()
            vals.append(int(line.strip()))

        grps_val[g] = vals

    totals = []
    for i in range(len(mon_MB_dirs)):
        total = 0
        for g in grp:
            total += grps_val[g][i]
        totals.append(total)

    grps_val["__grp_total__"] = totals
    return grps_val

def increase_bw(be_grp, i, percent):
    resctrl_par_dir = '/sys/fs/resctrl/%s/schemata' % be_grp[0]
    with open(resctrl_par_dir, 'r') as f:
        lines = f.readlines()

    for line in lines:
        if "MB:" in line:
            config = line.split(":")
            config = config[1].split(";")
            origin_p = int(config[i].split("=")[1])

    new_p = (origin_p + percent)
    if new_p > 100:
        new_p = 100
    elif new_p < 4:
        new_p = 4

    for g in be_grp:
        print("%s NUMA%d MB %d%%->%d%% delta %d%%" % (g, i, origin_p, new_p, percent))

        cnt = "MB:%d=%d" % (i, new_p)
        if new_p == origin_p:
            continue

        resctrl_par_dir = '/sys/fs/resctrl/%s/schemata' % g
        with open(resctrl_par_dir, 'w+') as f:
            f.write("%s\n" % cnt)

    return origin_p

def gain_numa_bw(lc_grp, be_grp, pid_ctl, adjust_enable, target_percent):
    lc_val = read_bw(lc_grp)
    be_val = read_bw(be_grp)

    bw_state = {}
    bw_state["lc_bw"] = lc_val
    bw_state["be_bw"] = be_val
    bw_state["total_bw"] = []

    numa_set = []

    for i in range(len(lc_val["__grp_total__"])):
        bw_state["total_bw"].append(lc_val["__grp_total__"][i] + be_val["__grp_total__"][i])

        set_state = {}

        if adjust_enable == True:
            if bw_state["lc_bw"]["__grp_total__"][i] < (0.02 * numa_bw_limit):
                # enlarge BE load
                diff = pid_ctl[i].max_output
            elif bw_state["lc_bw"]["__grp_total__"][i] > ((target_percent + 10) * numa_bw_limit / 100):
                # shutdown BE load
                diff = pid_ctl[i].min_output
            else:
                diff = pid_ctl[i].update(bw_state["total_bw"][i] * 100 // numa_bw_limit , 1)
        else:
            diff = 0

        curr = increase_bw(be_grp, i, diff)
        set_state['setting'] = curr
        numa_set.append(set_state)

    bw_list.append(bw_state)
    set_list.append(numa_set)
    return

def read_llc(grp):
    grps_val = {}

    for g in grp:
        vals = []

        resctrl_mon_data_dir = '/sys/fs/resctrl/%s/mon_data/' % g
        mon_data_dirs = os.listdir(resctrl_mon_data_dir)
        mon_llc_dirs = [dir for dir in mon_data_dirs if dir.startswith('mon_L3_')]
        mon_llc_dirs.sort()

        for mb_dir in mon_llc_dirs:
            with open(resctrl_mon_data_dir + mb_dir + '/llc_occupancy', 'r') as f:
            # with open(resctrl_mon_data_dir + mb_dir, 'r') as f:
                line = f.readline()
            vals.append(int(line.strip()))

        grps_val[g] = vals

    totals = []
    for i in range(len(mon_llc_dirs)):
        total = 0
        for g in grp:
            total += grps_val[g][i]
        totals.append(total)

    grps_val["__grp_total__"] = totals
    return grps_val

def gain_numa_llc(lc_grp, be_grp):
    lc_llc = read_llc(lc_grp)
    be_llc = read_llc(be_grp)

    llc_state = {}
    llc_state["lc_llc"] = lc_llc
    llc_state["be_llc"] = be_llc

    llc_list.append(llc_state)

    return

def gain_cpu(time1, time2):
    usage = calculate_cpu_utilization(time1, time2)
    print(f"CPU load: {usage:.2f}%")
    cpu_list.append(usage)
    return

def save_file(sig, frame):
    with open(f"ctrlgrp_bw.data", 'w') as fl:
        json.dump(bw_list, fl)

    with open(f"schemata_set.data", 'w') as fl:
        json.dump(set_list, fl)

    with open(f"ctrlgrp_llc.data", 'w') as fl:
        json.dump(llc_list, fl)

    with open(f"cpu_usage.data", 'w') as fl:
        json.dump(cpu_list, fl)

    exit(0)

def get_numa_count():
    return len(glob.glob("/sys/devices/system/node/node[0-9]*"))

def flatten_comma_separated(raw_list):
    out = []
    for item in raw_list:
        out.extend([x.strip() for x in item.split(",") if x.strip()])
    return out

def parse_args():
    parser = argparse.ArgumentParser(
                description="Example: read execution time and isolation percentage from CLI")

    parser.add_argument("-t", "--time", required=False, type=int,
                        help="Expected execution time in seconds, positive integer")
    parser.add_argument("-i", "--isolation", required=False, type=int,
                        help="Isolation percentage, integer between 1 and 100")
    parser.add_argument("-l", "--lc", action="append", default=["."],
                        help="Comma-separated or repeated latency-critical group names.")
    parser.add_argument("-b", "--be", action="append", default=[],
                        help="Comma-separated or repeated best-effort group names.")
    parser.add_argument("-v", "--version", action="version",
                        version=f"%(prog)s {__version__}")

    args = parser.parse_args()

    if args.time and args.time <= 0:
        sys.exit("ERROR: execution time must be > 0 seconds")

    if args.isolation and not 1 <= args.isolation <= 100:
        sys.exit("ERROR: isolation percentage must be between 0 and 100")

    if not args.be or not args.lc:
        sys.exit("ERROR: at least one -l or -b group name must be provided")

    args.lc = flatten_comma_separated(args.lc)
    args.be = flatten_comma_separated(args.be)

    return args

if __name__ == "__main__":
    signal.signal(signal.SIGINT, save_file)

    args = parse_args()
    exec_time = args.time
    target_percent = args.isolation
    lc_group = args.lc
    be_group = args.be

    adj_enable = False
    if target_percent:
        adj_enable = True

    pid_ctl = []
    numa_cnt = get_numa_count()
    for i in range(numa_cnt):
        pid_ctl.append(PID_Controller(kp=1.0, ki=0.02, kd=0.05,
                                      set_point=target_percent))

    now = 0
    time1 = read_cpu_times()

    while True:
        gain_numa_bw(lc_group, be_group, pid_ctl, adj_enable, target_percent)
        gain_numa_llc(lc_group, be_group)

        time2 = read_cpu_times()
        gain_cpu(time1, time2)
        time1 = time2

        print("..........................")
        time.sleep(1)
        now += 1

        if exec_time and exec_time <= now:
            break

    save_file(None, None)
