# SPDX-License-Identifier: GPL-2.0
#
# Analysis Lib for Dynamic Bandwidth Adjustment
#
# Copyright (C) 2025, Technologies Co., Ltd.
# Author: Zeng Heng <zengheng4@huawei.com>

import sys, json, argparse

__version__ = "1.0.0"

def get_data(file):
    try:
        with open(file, 'r') as f:
            data_list = json.load(f)
    except FileNotFoundError:
        return None

    return data_list

def parse_args():
    parser = argparse.ArgumentParser(description="Display selected NUMA's Analysis")
    parser.add_argument("-s", "--start", type=int, required=False,
                        help="Starting NUMA index (0-based)")
    parser.add_argument("-i", "--isolation", required=False, type=int,
                        help="Isolation target percentage, integer between 1 and 100")
    parser.add_argument("-v", "--version", action="version",
                        version=f"%(prog)s {__version__}")

    args = parser.parse_args()

    if args.start and args.start < 0:
        sys.exit("ERROR: --index must be non-negative")

    if args.isolation and not 1 <= args.isolation <= 100:
        sys.exit("ERROR: isolation percentage must be between 0 and 100")

    return args.start, args.isolation
