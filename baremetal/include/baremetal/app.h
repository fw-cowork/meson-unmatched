/* SPDX-License-Identifier: MIT */
#ifndef BAREMETAL_APP_H
#define BAREMETAL_APP_H

#include <baremetal/types.h>

/* Application entry called by the U-Boot go runtime. */
bm_ulong baremetal_main(int argc, char *const argv[]);

#endif
