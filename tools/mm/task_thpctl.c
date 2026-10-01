// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * task_thpctl.c - Show or change the thp behavior of a process
 *
 * Copyright (C) 2024 Nanyong Sun <sunnanyong@huawei.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <linux/thp_ctl.h>

enum thp_actions {
	ACTION_NONE,
	GET_THP_STATUS,
	THP_SET_DISABLE,
	THP_SET_ENABLE,
};

#define THP_CTL_PATH "/dev/thp_ctl"

static void print_usage(void)
{
	printf("Usage:\n");
	printf("  task_thpctl [options]\n");
	printf("\n");
	printf("Description:\n");
	printf("  Show or change the thp behavior of a process.\n");
	printf("\n");
	printf("Options:\n");
	printf(" -h, --help                     This message.\n");
	printf(" -p, --pid <pid>                Operate on existing given pid.\n");
	printf(" -g, --get_thp_status           Display thp status of a process\n");
	printf("                                specified by --pid.\n");
	printf(" -s, --thp_set_enable           Set thp enable for a process\n");
	printf("                                specified by --pid.\n");
	printf(" -d, --thp_set_disable          Set thp disable for a process\n");
	printf("                                specified by --pid.\n");
	printf("\n");
	printf("Examples:\n");
	printf("  task_thpctl -s -p <pid>\n");
	printf("  task_thpctl -d -p <pid>\n");
}

static void exit_with_help(void)
{
	fprintf(stderr, "Try 'task_thpctl --help' for more information.\n");
	exit(EXIT_FAILURE);
}

static void get_task_thp_status(int fd, pid_t pid)
{
	struct get_thp_status_arg stat = {
		.pid = pid,
		.thp_enable = 0
	};
	int err;

	err = ioctl(fd, IOC_THP_STATUS_GET, &stat);
	if (err < 0) {
		fprintf(stderr, "Task:%d get thp status failed: %s\n",
				pid, strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Task %d thp status:\n", pid);
	printf("    thp_enable: %ld\n", stat.thp_enable);
}

static void set_task_thp_enable(int fd, pid_t pid, bool enable)
{
	unsigned int cmd = enable ? IOC_THP_SET_ENABLE : IOC_THP_SET_DISABLE;
	int err;

	err = ioctl(fd, cmd, &pid);
	if (err < 0) {
		fprintf(stderr, "Task:%d set thp %s failed: %s\n",
				pid, enable ? "enable" : "disable",
				strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static const char optstring[] = "+hp:gsd";
static const struct option longopts[] = {
	{"help",             0, NULL, 'h'},
	{"pid",              required_argument, NULL, 'p'},
	{"get_thp_status",   0, NULL, 'g'},
	{"thp_set_enable",   0, NULL, 's'},
	{"thp_set_disable",  0, NULL, 'd'},
	{0,                  0, NULL,  0}
};

int main(int argc, char **argv)
{
	enum thp_actions action = ACTION_NONE;
	pid_t pid = 0;
	int opt, fd;

	while ((opt = getopt_long(argc, argv, optstring, longopts, NULL)) != -1) {
		switch (opt) {
		case 'p':
			if (optarg)
				pid = atoi(optarg);
			if (!pid) {
				fprintf(stderr, "invalid PID argument\n");
				exit_with_help();
			}
			break;
		case 's':
			action = THP_SET_ENABLE;
			break;
		case 'd':
			action = THP_SET_DISABLE;
			break;
		case 'g':
			action = GET_THP_STATUS;
			break;
		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
		default:
			exit_with_help();
		}
	}

	if (action == ACTION_NONE || !pid)
		exit_with_help();

	fd = open(THP_CTL_PATH, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Open %s failed: %s\n",
				THP_CTL_PATH, strerror(errno));
		exit(EXIT_FAILURE);
	}

	switch (action) {
	case GET_THP_STATUS:
		get_task_thp_status(fd, pid);
		exit(EXIT_SUCCESS);
	case THP_SET_ENABLE:
		set_task_thp_enable(fd, pid, true);
		break;
	case THP_SET_DISABLE:
		set_task_thp_enable(fd, pid, false);
		break;
	default:
		exit_with_help();
	}

	close(fd);

	return EXIT_SUCCESS;
}
