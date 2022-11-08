/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

#include "ufs_cmds.h"
#include "options.h"
#include "ufs.h"
#include "ufs_err_hist.h"
#include "unipro.h"
#include "ufs_ffu.h"
#include "ufs_vendor.h"
#include "ufs_rpmb.h"
#include "ufs_hmr.h"

#define UFS_BSG_UTIL_VERSION	"1.9"
typedef int (*command_function)(struct tool_options *opt);

struct tool_command {
	command_function func; /* function which implements the command */
	char *conf_type; /* one of: descriptor/attributes/flags */
	int conf_type_ind; /* confiruration type index */
};

static struct tool_command commands[] = {
	/*
	 * avoid short commands different for the case only
	 */
	{ do_desc, "desc", DESC_TYPE},
	{ do_attributes, "attr", ATTR_TYPE},
	{ do_flags, "fl", FLAG_TYPE},
	{ do_err_hist, "err_hist", ERR_HIST_TYPE},
	{ do_uic, "uic", UIC_TYPE},
	{ do_ffu, "ffu", FFU_TYPE},
	{ do_vendor, "vendor", VENDOR_BUFFER_TYPE},
	{ do_rpmb, "rpmb", RPMB_CMD_TYPE},
	{ do_hmr, "hmr", HMR_TYPE},
	{ 0, 0, 0}
};

static char *get_prgname(char *programname)
{
	char	*np;

	np = strrchr(programname, '/');
	if (!np)
		np = programname;
	else
		np++;

	return np;
}

static void help(char *np)
{
	char help_str[256] = {0};

	strcat(help_str, "<desc | attr | fl | err_hist | uic | ffu | vendor | "
		"rpmb | hmr>");
	printf("\n Usage:\n");
	printf("\n\t%s help|--help|-h\n\t\tShow the help.\n", np);
	printf("\n\t%s -v\n\t\tShow the version.\n", np);
	printf("\n\t%s %s%s", np, help_str,
		" --help|-h\n\t\tShow detailed help for a command\n");
}

static void initialized_options(struct tool_options *options)
{
	memset(options, INVALID, sizeof(*options));
	options->path[0] = '\0';
	options->keypath[0] = '\0';
	options->data = NULL;
	options->sg_type = SG4_TYPE;
}

static int parse_args(int argc, char **argv, command_function *func,
		struct tool_options *options)
{
	int rc = OK;
	struct tool_command *cp;
	char *prgname = get_prgname(argv[0]);

	if (argc == 2 && !strcmp(argv[1], "-v")) {
		printf("\n\t %s ver: %s\n", prgname, UFS_BSG_UTIL_VERSION);
		goto out;
	} else if (argc <= 2) {
		help(prgname);
		goto out;
	}

	for (cp = commands; cp->conf_type; cp++) {
		if (!strcmp(argv[1], cp->conf_type)) {
			options->config_type_inx = cp->conf_type_ind;
			*func = cp->func;
			break;
		}
	}

	if (options->config_type_inx == INVALID) {
		print_error("Please enter the correct config type");
		help(prgname);
		rc = -EINVAL;
		goto out;
	}

	if (argc == 3 &&
		(!strcmp(argv[2], "-h") || !strcmp(argv[2], "--help"))) {
		print_command_help(prgname, options->config_type_inx);
		*func = 0;
		goto out;
	}

	rc = init_options(argc, argv, options);

out:
	return rc;
}

int write_file(const char *name, const void *buffer, int length)
{
	int fd;
	int rc = 0;
	size_t ret;

	WRITE_LOG("writing file %s length=%d\n", name, length);
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC | O_SYNC, 0600);
	if (fd == -1) {
		WRITE_LOG("%s: failed in open errno=%d", __func__, errno);
		return -ENOENT;
	}

	ret = write(fd, buffer, length);
	if (length != ret) {
		WRITE_LOG( "%s: failed in write errno=%d", __func__, errno);
		rc = -EIO;
	}

	close(fd);
	return rc;
}

void print_error(const char *msg, ...)
{
	va_list args;

	printf("\n Err: ");
	va_start(args, msg);
	vprintf(msg, args);
	va_end(args);
	printf("\n");
}

void print_warn(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	fprintf(stderr, "\nWARN: ");
	vfprintf(stderr, msg, args);
	va_end(args);
	printf("\n");
}

void print_command_help(char *prgname, int config_type)
{
	switch (config_type) {
	case DESC_TYPE:
		desc_help(prgname);
		break;
	case ATTR_TYPE:
		attribute_help(prgname);
		break;
	case FLAG_TYPE:
		flag_help(prgname);
		break;
	case ERR_HIST_TYPE:
		err_hist_help(prgname);
		break;
	case FFU_TYPE:
		ffu_help(prgname);
		break;
	case UIC_TYPE:
		unipro_help(prgname);
		break;
	case VENDOR_BUFFER_TYPE:
		vendor_help(prgname);
		break;
	case RPMB_CMD_TYPE:
		rpmb_help(prgname);
		break;
	case HMR_TYPE:
		hmr_help(prgname);
		break;
	default:
		print_error("Unsupported cmd type");
		break;
	}
}

/*
 * Wrapper for strtol() function.
 *
 * strtol() has advantages over atoi():
 * 	- has error handling
 *	- handles not only decimal, but acts accordingly to the 'base' argument
 *	- accepts strings with "0x" prefix
 *	- stores address of the first invalid character
 */
long str_to_long(char *nptr, int base, long *result)
{
	char *endptr;

	if (!nptr || !result)
		return ERROR;

	/*
	 * From man:
	 * Since strtol() can legitimately return 0, LONG_MAX, or LONG_MIN
	 * on both success and failure, the calling program should set errno to 0
	 * before the call...
	 */
	errno = 0;

	*result = strtol(optarg, &endptr, base);

	if (endptr == nptr ||		/* no conversion performed */
		*endptr != '\0' ||		/* some chars not converted */
		*result == LONG_MIN ||	/* underflow occured */
		*result == LONG_MAX ||	/* overflow occured */
		errno != 0)				/* any other error */
			return ERROR;

	return OK;
}

int main(int ac, char **av)
{
	int rc;
	command_function func = NULL;
	struct tool_options options;

	initialized_options(&options);

	rc = parse_args(ac, av, &func, &options);
	if (rc)
		goto out;

	if (func)
		rc = func(&options);

out:
	if (options.data)
		free(options.data);
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}

