/***************************************************************************
                          libproxychains.c  -  description
                             -------------------
    begin                : Tue May 14 2002
    copyright          :  netcreature (C) 2002
    email                 : netcreature@users.sourceforge.net
 ***************************************************************************/
 /*     GPL */
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#undef _GNU_SOURCE
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/param.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "core.h"
#include "common.h"

#define     satosin(x)      ((struct sockaddr_in *) &(x))
#define     SOCKADDR(x)     (satosin(x)->sin_addr.s_addr)
#define     SOCKADDR_2(x)     (satosin(x)->sin_addr)
#define     SOCKPORT(x)     (satosin(x)->sin_port)
#define     SOCKFAMILY(x)     (satosin(x)->sin_family)
#define     MAX_CHAIN 512

connect_t true_connect;
gethostbyname_t true_gethostbyname;
getaddrinfo_t true_getaddrinfo;
freeaddrinfo_t true_freeaddrinfo;
getnameinfo_t true_getnameinfo;
gethostbyaddr_t true_gethostbyaddr;

int tcp_read_time_out;
int tcp_connect_time_out;
int proxychains_got_chain_data = 0;
int proxychains_quiet_mode = 0;
int proxychains_resolver = 0;

unsigned int proxychains_proxy_count = 0;
unsigned int proxychains_max_chain = 1;
unsigned int remote_dns_subnet = 224;

localaddr_arg localnet_addr[MAX_LOCALNET];
chain_type proxychains_ct;
proxy_data proxychains_pd[MAX_CHAIN];

size_t num_localnet_addr = 0;

#ifdef THREAD_SAFE
pthread_once_t init_once = PTHREAD_ONCE_INIT;
#endif
static int init_l = 0;

static void load_default_settings(chain_type *ct);
static inline void get_chain_data(proxy_data * pd, unsigned int *proxy_count, chain_type * ct);
static void simple_socks5_env(proxy_data * pd, unsigned int *proxy_count, chain_type * ct);

static void* load_sym(char* symname, void* proxyfunc) {

	void *funcptr = dlsym(RTLD_NEXT, symname);
	if(!funcptr) {
		fprintf(stderr, "Cannot load symbol '%s' %s\n", symname, dlerror());
		exit(1);
	} else {
		PDEBUG("loaded symbol '%s'" " real addr %p  wrapped addr %p\n", symname, funcptr, proxyfunc);
	}
	if(funcptr == proxyfunc) {
		PDEBUG("circular reference detected, aborting!\n");
		abort();
	}
	return funcptr;
}

#define INIT() init_lib_wrapper(__FUNCTION__)

#define SETUP_SYM(X) do { true_ ## X = load_sym( # X, X ); } while(0)

static void do_init(void) {
	MUTEX_INIT(&internal_ips_lock, NULL);

#ifdef __APPLE__
	MUTEX_INIT(&internal_getsrvbyname_lock, NULL);
#endif

	/* check for simple SOCKS5 proxy setup */
	simple_socks5_env(proxychains_pd, &proxychains_proxy_count, &proxychains_ct);

	/* read the config file */
	get_chain_data(proxychains_pd, &proxychains_proxy_count, &proxychains_ct);

	proxychains_write_log(LOG_PREFIX "DLL init\n");
	SETUP_SYM(connect);
	SETUP_SYM(gethostbyname);
	SETUP_SYM(getaddrinfo);
	SETUP_SYM(freeaddrinfo);
	SETUP_SYM(gethostbyaddr);
	SETUP_SYM(getnameinfo);
	init_l = 1;
}

static void init_lib_wrapper(const char* caller) {
#ifndef DEBUG
	(void) caller;
#endif
#ifndef THREAD_SAFE
	if(init_l) return;
	PDEBUG("%s called from %s\n", __FUNCTION__,  caller);
	do_init();
#else
	if(!init_l) PDEBUG("%s called from %s\n", __FUNCTION__,  caller);
	pthread_once(&init_once, do_init);
#endif
}

/* if we use gcc >= 3, we can instruct the dynamic loader
 * to call init_lib at link time. otherwise it gets loaded
 * lazily, which has the disadvantage that there's a potential
 * race condition if 2 threads call it before init_l is set
 * and PTHREAD support was disabled */
#if __GNUC__ > 2
__attribute__((constructor))
static void gcc_init(void) {
	INIT();
}
#endif

FILE *
open_config_file() {
	char home_conf[MAXPATHLEN], prefix_conf[MAXPATHLEN];
	FILE *file;

	snprintf(home_conf, 256, "%s/.proxychains/proxychains.conf", getenv("HOME"));
	snprintf(prefix_conf, 256, "%s/etc/proxychains.conf", INSTALL_PREFIX);

	if(!(file = fopen("./proxychains.conf", "r"))) {
		if(!(file = fopen(home_conf, "r"))) {
			if(!(file = fopen(prefix_conf, "r"))) {
				if(!(file = fopen("/etc/proxychains.conf", "r"))) {
					perror("Can't locate proxychains.conf");
					exit(1);
				}
			}
		}
	}

	return file;
}

/* default settings common to get_chain_data and simple_socks5_env */
static void load_default_settings(chain_type *ct) {
	char *env;

	tcp_read_time_out = 4 * 1000;
	tcp_connect_time_out = 10 * 1000;
	*ct = DYNAMIC_TYPE;

	env = getenv(PROXYCHAINS_QUIET_MODE_ENV_VAR);
	if(env && *env == '1')
		proxychains_quiet_mode = 1;
}

/* get configuration from config file */
static void get_chain_data(proxy_data * pd, unsigned int *proxy_count, chain_type * ct) {
	unsigned int count = 0;
	int port_n = 0, list = 0;
	char buff[1024], type[1024], host[1024], user[1024];
	char *env;
	char local_in_addr_port[32];
	char local_in_addr[32], local_in_port[32], local_netmask[32];
	FILE *file = NULL;

	if(proxychains_got_chain_data)
		return;

	load_default_settings(ct);

	env = get_config_path(getenv(PROXYCHAINS_CONF_FILE_ENV_VAR), buff, sizeof(buff));
	file = fopen(env, "r");

	while(fgets(buff, sizeof(buff), file)) {
		if(buff[0] != '\n' && buff[strspn(buff, " ")] != '#') {
			/* proxylist has to come last */
			if(list) {
				if(count >= MAX_CHAIN)
					break;

				memset(&pd[count], 0, sizeof(proxy_data));

				pd[count].ps = PLAY_STATE;
				port_n = 0;

				sscanf(buff, "%s %s %d %s %s", type, host, &port_n, pd[count].user, pd[count].pass);

				pd[count].ip.as_int = (uint32_t) inet_addr(host);
				pd[count].port = htons((unsigned short) port_n);

				if(!strcmp(type, "http")) {
					pd[count].pt = HTTP_TYPE;
				} else if(!strcmp(type, "socks4")) {
					pd[count].pt = SOCKS4_TYPE;
				} else if(!strcmp(type, "socks5")) {
					pd[count].pt = SOCKS5_TYPE;
				} else
					continue;

				if(pd[count].ip.as_int && port_n && pd[count].ip.as_int != (uint32_t) - 1)
					count++;
			} else {
				if(strstr(buff, "[ProxyList]")) {
					list = 1;
				} else if(strstr(buff, "random_chain")) {
					*ct = RANDOM_TYPE;
				} else if(strstr(buff, "strict_chain")) {
					*ct = STRICT_TYPE;
				} else if(strstr(buff, "dynamic_chain")) {
					*ct = DYNAMIC_TYPE;
				} else if(strstr(buff, "tcp_read_time_out")) {
					sscanf(buff, "%s %d", user, &tcp_read_time_out);
				} else if(strstr(buff, "tcp_connect_time_out")) {
					sscanf(buff, "%s %d", user, &tcp_connect_time_out);
				} else if(strstr(buff, "remote_dns_subnet")) {
					sscanf(buff, "%s %d", user, &remote_dns_subnet);
					if(remote_dns_subnet >= 256) {
						fprintf(stderr,
							"remote_dns_subnet: invalid value. requires a number between 0 and 255.\n");
						exit(1);
					}
				} else if(strstr(buff, "localnet")) {
					if(sscanf(buff, "%s %21[^/]/%15s", user, local_in_addr_port, local_netmask) < 3) {
						fprintf(stderr, "localnet format error");
						exit(1);
					}
					/* clean previously used buffer */
					memset(local_in_port, 0, sizeof(local_in_port) / sizeof(local_in_port[0]));

					if(sscanf(local_in_addr_port, "%15[^:]:%5s", local_in_addr, local_in_port) < 2) {
						PDEBUG("added localnet: netaddr=%s, netmask=%s\n",
						       local_in_addr, local_netmask);
					} else {
						PDEBUG("added localnet: netaddr=%s, port=%s, netmask=%s\n",
						       local_in_addr, local_in_port, local_netmask);
					}
					if(num_localnet_addr < MAX_LOCALNET) {
						int error;
						error =
						    inet_pton(AF_INET, local_in_addr,
							      &localnet_addr[num_localnet_addr].in_addr);
						if(error <= 0) {
							fprintf(stderr, "localnet address error\n");
							exit(1);
						}
						error =
						    inet_pton(AF_INET, local_netmask,
							      &localnet_addr[num_localnet_addr].netmask);
						if(error <= 0) {
							fprintf(stderr, "localnet netmask error\n");
							exit(1);
						}
						if(local_in_port[0]) {
							localnet_addr[num_localnet_addr].port =
							    (unsigned short) atoi(local_in_port);
						} else {
							localnet_addr[num_localnet_addr].port = 0;
						}
						++num_localnet_addr;
					} else {
						fprintf(stderr, "# of localnet exceed %d.\n", MAX_LOCALNET);
					}
				} else if(strstr(buff, "chain_len")) {
					char *pc;
					int len;
					pc = strchr(buff, '=');
					len = atoi(++pc);
					proxychains_max_chain = (unsigned int) (len ? len : 1);
				} else if(strstr(buff, "quiet_mode")) {
					proxychains_quiet_mode = 1;
				} else if(strstr(buff, "proxy_dns")) {
					proxychains_resolver = 1;
				}
			}
		}
	}
	fclose(file);
	*proxy_count = count;
	proxychains_got_chain_data = 1;
}

static void simple_socks5_env(proxy_data *pd, unsigned int *proxy_count, chain_type *ct) {
	char *port_string;

	if(proxychains_got_chain_data)
		return;

	load_default_settings(ct);

	port_string = getenv(PROXYCHAINS_SOCKS5_ENV_VAR);

	if(!port_string)
		return;

	memset(pd, 0, sizeof(proxy_data));

	pd[0].ps = PLAY_STATE;
	pd[0].ip.as_int = (uint32_t) inet_addr("127.0.0.1");
	pd[0].port = htons((unsigned short) strtol(port_string, NULL, 0));
	pd[0].pt = SOCKS5_TYPE;
	proxychains_max_chain = 1;

	if(getenv(PROXYCHAINS_DNS_ENV_VAR))
		proxychains_resolver = 1;

	*proxy_count = 1;
	proxychains_got_chain_data = 1;
}

/*******  HOOK FUNCTIONS  *******/

int connect(int sock, const struct sockaddr *addr, socklen_t len) {
	int socktype = 0, flags = 0, ret = 0;
	socklen_t optlen = 0;
	ip_type dest_ip;
#ifdef DEBUG
	char str[256];
#endif
	struct in_addr *p_addr_in;
	unsigned short port;
	size_t i;
	int remote_dns_connect = 0;

	INIT();
	optlen = sizeof(socktype);
	getsockopt(sock, SOL_SOCKET, SO_TYPE, &socktype, &optlen);
	if(!(SOCKFAMILY(*addr) == AF_INET && socktype == SOCK_STREAM))
		return true_connect(sock, addr, len);

	p_addr_in = &((struct sockaddr_in *) addr)->sin_addr;
	port = ntohs(((struct sockaddr_in *) addr)->sin_port);

#ifdef DEBUG
//      PDEBUG("localnet: %s; ", inet_ntop(AF_INET,&in_addr_localnet, str, sizeof(str)));
//      PDEBUG("netmask: %s; " , inet_ntop(AF_INET, &in_addr_netmask, str, sizeof(str)));
	PDEBUG("target: %s\n", inet_ntop(AF_INET, p_addr_in, str, sizeof(str)));
	PDEBUG("port: %d\n", port);
#endif

	// check if connect called from proxydns
        remote_dns_connect = (ntohl(p_addr_in->s_addr) >> 24 == remote_dns_subnet);

	for(i = 0; i < num_localnet_addr && !remote_dns_connect; i++) {
		if((localnet_addr[i].in_addr.s_addr & localnet_addr[i].netmask.s_addr)
		   == (p_addr_in->s_addr & localnet_addr[i].netmask.s_addr)) {
			if(!localnet_addr[i].port || localnet_addr[i].port == port) {
				PDEBUG("accessing localnet using true_connect\n");
				return true_connect(sock, addr, len);
			}
		}
	}

	flags = fcntl(sock, F_GETFL, 0);
	if(flags & O_NONBLOCK)
		fcntl(sock, F_SETFL, !O_NONBLOCK);

	dest_ip.as_int = SOCKADDR(*addr);

	ret = connect_proxy_chain(sock,
				  dest_ip,
				  SOCKPORT(*addr),
				  proxychains_pd, proxychains_proxy_count, proxychains_ct, proxychains_max_chain);

	fcntl(sock, F_SETFL, flags);
	if(ret != SUCCESS)
		errno = ECONNREFUSED;
	return ret;
}

static struct gethostbyname_data ghbndata;
struct hostent *gethostbyname(const char *name) {
	INIT();

	PDEBUG("gethostbyname: %s\n", name);

	if(proxychains_resolver)
		return proxy_gethostbyname(name, &ghbndata);
	else
		return true_gethostbyname(name);

	return NULL;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
	int ret = 0;

	INIT();

	PDEBUG("getaddrinfo: %s %s\n", node, service);

	if(proxychains_resolver)
		ret = proxy_getaddrinfo(node, service, hints, res);
	else
		ret = true_getaddrinfo(node, service, hints, res);

	return ret;
}

void freeaddrinfo(struct addrinfo *res) {
	INIT();

	PDEBUG("freeaddrinfo %p \n", res);

	if(!proxychains_resolver)
		true_freeaddrinfo(res);
	else
		proxy_freeaddrinfo(res);
	return;
}

// work around a buggy prototype in GLIBC. according to the bugtracker it has been fixed in git at 02 May 2011.
// 2.14 came out in June 2011 so that should be the first fixed version
#if defined(__GLIBC__) && (__GLIBC__ < 3) && (__GLIBC_MINOR__ < 14)
int getnameinfo(const struct sockaddr *sa,
		socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, unsigned int flags)
#else
int getnameinfo(const struct sockaddr *sa,
		socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags)
#endif
{
	char ip_buf[16];
	int ret = 0;

	INIT();

	PDEBUG("getnameinfo: %s %s\n", host, serv);

	if(!proxychains_resolver) {
		ret = true_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
	} else {
		if(hostlen) {
			inet_ntop(AF_INET, (unsigned char*) &(SOCKADDR_2(*sa)), ip_buf, sizeof(ip_buf));
			strncpy(host, ip_buf, hostlen);
		}
		if(servlen)
			snprintf(serv, servlen, "%d", ntohs(SOCKPORT(*sa)));
	}
	return ret;
}

#if (defined __linux__) || (defined __APPLE__)
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type) {
#else
struct hostent *gethostbyaddr(const char *addr, socklen_t len, int type) {
#endif
	static char buf[16];
	static char ipv4[4];
	static char *list[2];
	static struct hostent he;

	INIT();

	PDEBUG("TODO: proper gethostbyaddr hook\n");

	if(!proxychains_resolver)
		return true_gethostbyaddr(addr, len, type);
	else {

		PDEBUG("len %u\n", len);
		if(len != 4)
			return NULL;
		he.h_name = buf;
		memcpy(ipv4, addr, 4);
		list[0] = ipv4;
		list[1] = NULL;
		he.h_addr_list = list;
		he.h_addrtype = AF_INET;
		he.h_aliases = NULL;
		he.h_length = 4;
		inet_ntop(AF_INET, addr, buf, sizeof(buf));
		return &he;
	}
	return NULL;
}
