/*   (C) 2011, 2012 rofl0r
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700

#include <sys/types.h>
#include <sys/param.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#include "common.h"

static int usage(char **argv) {
	printf("\nUsage:\t%s -q -f config_file program_name [arguments]\n"
	       "\t-q makes proxychains quiet - this overrides the config setting\n"
	       "\t-f allows to manually specify a configfile to use\n"
	       "\tfor example : proxychains telnet somehost.com\n" "More help in README file\n\n", argv[0]);
	return EXIT_FAILURE;
}

static const char *dll_name = DLL_NAME;

static char own_dir[256];
static const char *dll_dirs[] = {
	own_dir,
	LIB_DIR,
	"/lib",
	"/usr/lib",
	"/usr/local/lib",
	"/lib64",
	NULL
};

static void set_own_dir(const char *argv0) {
	size_t l = strlen(argv0);
	while(l && argv0[l - 1] != '/')
		l--;
	if(l == 0)
		memcpy(own_dir, "/dev/null/", 2);
	else {
		memcpy(own_dir, argv0, l - 1);
		own_dir[l] = 0;
	}
}

#define MAX_COMMANDLINE_FLAGS 2

int main(int argc, char *argv[]) {
	char *path = NULL;
	char buf[PATH_MAX];
	char pbuf[PATH_MAX];
	int start_argv = 1;
	int quiet = 0;
	size_t i;

	const char *prefix = NULL;

	for(i = 0; i < MAX_COMMANDLINE_FLAGS; i++) {
		if(start_argv < argc && argv[start_argv][0] == '-') {
			if(argv[start_argv][1] == 'q') {
				quiet = 1;
				start_argv++;
			} else if(argv[start_argv][1] == 'f') {

				if(start_argv + 1 < argc)
					path = argv[start_argv + 1];
				else
					return usage(argv);

				start_argv += 2;
			}
		} else
			break;
	}

	if(start_argv >= argc)
		return usage(argv);

	/* check if path of config file has not been passed via command line */
	path = get_config_path(path, pbuf, sizeof(pbuf));
	if(!quiet)
		fprintf(stderr, LOG_PREFIX "config file found: %s\n", path);

	/* Set PROXYCHAINS_CONF_FILE to get proxychains lib to use new config file. */
	setenv(PROXYCHAINS_CONF_FILE_ENV_VAR, path, 1);

	if(quiet)
		setenv(PROXYCHAINS_QUIET_MODE_ENV_VAR, "1", 1);

	// search DLL
	set_own_dir(argv[0]);

	while(dll_dirs[i]) {
		snprintf(buf, sizeof(buf), "%s/%s", dll_dirs[i], dll_name);
		if(access(buf, R_OK) != -1) {
			prefix = dll_dirs[i];
			break;
		}
		i++;
	}

	if(!prefix) {
		fprintf(stderr, "couldnt locate %s\n", dll_name);
		return EXIT_FAILURE;
	}

	if(!quiet)
		fprintf(stderr, LOG_PREFIX "preloading %s/%s\n", prefix, dll_name);

#ifndef IS_MAC
	snprintf(buf, sizeof(buf), "%s/%s", prefix, dll_name);
	setenv("LD_PRELOAD", buf, 1);
#else
	snprintf(buf, sizeof(buf), "%s/%s", prefix, dll_name);
	setenv("DYLD_INSERT_LIBRARIES", buf, 1);
	setenv("DYLD_FORCE_FLAT_NAMESPACE", "1", 1);
#endif
	execvp(argv[start_argv], &argv[start_argv]);
	perror("proxychains can't load process....");

	return EXIT_FAILURE;
}
