// SPDX-License-Identifier: MIT

#include <baremetal/string.h>

int bm_streq(const char *left, const char *right)
{
	while (*left && *left == *right) {
		left++;
		right++;
	}

	return *left == *right;
}
