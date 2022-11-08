/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include "options.h"

#ifndef UFS_RPMB_H_
#define UFS_RPMB_H_

enum rpmb_cmd_type {
	AUTHENTICATION_KEY = 0,
	READ_WRITE_COUNTER,
	READ_RPMB,
	WRITE_RPMB,
	WRITE_SEC_RPMB_CONF_BLOCK,
	READ_SEC_RPMB_CONF_BLOCK,
	RPMB_CMD_MAX
};

void rpmb_help(char *tool_name);
int do_rpmb(struct tool_options *opt);

#endif /* UFS_RPMB_H_ */
