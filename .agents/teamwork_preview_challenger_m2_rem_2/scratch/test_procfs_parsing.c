/*
 * test_procfs_parsing.c - Empirical test harness for procfs write command parsing
 * Verifies exact string matching and clean rejection of partial/prefix matches.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>

// Mirror kernel's strim implementation
static char *kernel_strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace((unsigned char)*end))
		end--;
	*(end + 1) = '\0';

	while (*s && isspace((unsigned char)*s))
		s++;

	return s;
}

// Simulated safety_mem_proc_write state
static bool g_safety_protected = false;
static int g_safety_write_calls = 0;

static int test_safety_mem_proc_write(const char *buf, size_t count)
{
	char kbuf[16] = {0};
	char *cmd;

	if (count >= sizeof(kbuf))
		return -1; // -EINVAL

	memcpy(kbuf, buf, count);
	kbuf[count] = '\0';
	cmd = kernel_strim(kbuf);

	if (strcmp(cmd, "1") == 0 || strcmp(cmd, "protect") == 0) {
		g_safety_protected = true;
		g_safety_write_calls++;
	} else if (strcmp(cmd, "0") == 0 || strcmp(cmd, "unprotect") == 0) {
		g_safety_protected = false;
		g_safety_write_calls++;
	}

	return count;
}

// Simulated bad_driver_proc_write state
static int g_bad_driver_attack_mode = 0;
static int g_bad_driver_calls = 0;

static int test_bad_driver_proc_write(const char *buf, size_t count)
{
	char kbuf[16] = {0};
	char *cmd;

	if (count >= sizeof(kbuf))
		return -1; // -EINVAL

	memcpy(kbuf, buf, count);
	kbuf[count] = '\0';
	cmd = kernel_strim(kbuf);

	if (strcmp(cmd, "1") == 0) {
		g_bad_driver_attack_mode = 1;
		g_bad_driver_calls++;
	} else if (strcmp(cmd, "2") == 0) {
		g_bad_driver_attack_mode = 2;
		g_bad_driver_calls++;
	} else if (strcmp(cmd, "3") == 0) {
		g_bad_driver_attack_mode = 3;
		g_bad_driver_calls++;
	}

	return count;
}

int main(void)
{
	printf("=== Running Procfs Command Parsing Test Harness ===\n\n");

	// 1. Valid exact commands for safety_mem
	struct {
		const char *input;
		bool expected_protected;
		bool should_match;
	} safety_test_cases[] = {
		{"1", true, true},
		{"1\n", true, true},
		{"protect", true, true},
		{"protect\n", true, true},
		{"  protect  \n", true, true},
		{"0", false, true},
		{"0\n", false, true},
		{"unprotect", false, true},
		{"unprotect\n", false, true},
		// Partial / prefix invalid matches
		{"10", false, false},
		{"10\n", false, false},
		{"123", false, false},
		{"00", false, false},
		{"protect_all", false, false},
		{"protection", false, false},
		{"unprotect_now", false, false},
		{"1_extra", false, false},
		{"0_extra", false, false},
		{NULL, false, false}
	};

	int failed = 0;

	for (int i = 0; safety_test_cases[i].input != NULL; i++) {
		g_safety_protected = false; // reset
		g_safety_write_calls = 0;

		const char *in = safety_test_cases[i].input;
		int res = test_safety_mem_proc_write(in, strlen(in));

		if (safety_test_cases[i].should_match) {
			if (g_safety_write_calls != 1 || g_safety_protected != safety_test_cases[i].expected_protected) {
				printf("[FAIL] safety_mem: Input '%s' expected match (protected=%d), got write_calls=%d, protected=%d\n",
				       in, safety_test_cases[i].expected_protected, g_safety_write_calls, g_safety_protected);
				failed++;
			} else {
				printf("[PASS] safety_mem: Input '%s' correctly matched (protected=%d)\n", in, g_safety_protected);
			}
		} else {
			if (g_safety_write_calls != 0) {
				printf("[FAIL] safety_mem: Input '%s' SHOULD BE REJECTED but triggered action! write_calls=%d, protected=%d\n",
				       in, g_safety_write_calls, g_safety_protected);
				failed++;
			} else {
				printf("[PASS] safety_mem: Input '%s' cleanly REJECTED (no action triggered)\n", in);
			}
		}
	}

	printf("\n--- Bad Driver Command Test ---\n");

	struct {
		const char *input;
		int expected_mode;
		bool should_match;
	} bad_driver_test_cases[] = {
		{"1", 1, true},
		{"1\n", 1, true},
		{"2", 2, true},
		{"2\n", 2, true},
		{"3", 3, true},
		{"3\n", 3, true},
		{"  2 \n", 2, true},
		// Partial / prefix invalid matches
		{"10", 0, false},
		{"10\n", 0, false},
		{"20", 0, false},
		{"30", 0, false},
		{"123", 0, false},
		{"2_attack", 0, false},
		{"3_mode", 0, false},
		{NULL, 0, false}
	};

	for (int i = 0; bad_driver_test_cases[i].input != NULL; i++) {
		g_bad_driver_attack_mode = 0; // reset
		g_bad_driver_calls = 0;

		const char *in = bad_driver_test_cases[i].input;
		test_bad_driver_proc_write(in, strlen(in));

		if (bad_driver_test_cases[i].should_match) {
			if (g_bad_driver_calls != 1 || g_bad_driver_attack_mode != bad_driver_test_cases[i].expected_mode) {
				printf("[FAIL] bad_driver: Input '%s' expected mode %d, got calls=%d, mode=%d\n",
				       in, bad_driver_test_cases[i].expected_mode, g_bad_driver_calls, g_bad_driver_attack_mode);
				failed++;
			} else {
				printf("[PASS] bad_driver: Input '%s' correctly matched (mode=%d)\n", in, g_bad_driver_attack_mode);
			}
		} else {
			if (g_bad_driver_calls != 0) {
				printf("[FAIL] bad_driver: Input '%s' SHOULD BE REJECTED but triggered mode %d!\n",
				       in, g_bad_driver_attack_mode);
				failed++;
			} else {
				printf("[PASS] bad_driver: Input '%s' cleanly REJECTED (no attack mode triggered)\n", in);
			}
		}
	}

	if (failed == 0) {
		printf("\n=== VERDICT: ALL PROCFS PARSING TESTS PASSED ===\n");
		return 0;
	} else {
		printf("\n=== VERDICT: %d PROCFS PARSING TESTS FAILED ===\n", failed);
		return 1;
	}
}
