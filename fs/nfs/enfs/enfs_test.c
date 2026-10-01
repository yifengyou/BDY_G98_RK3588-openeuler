// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>

bool enfs_test_reconnect_time(void);

static void enfs_unstable_state_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, true, enfs_test_reconnect_time());
}

static struct kunit_case enfs_test_cases[] = {
	KUNIT_CASE(enfs_unstable_state_test),
	{}
};

static struct kunit_suite enfs_suite = {
	.name = "enfs",
	.test_cases = enfs_test_cases,
};

kunit_test_suite(enfs_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for ENFS");
