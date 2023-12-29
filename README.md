# ProxyChains ver. 4.3.0 README

![build-badge](https://github.com/haad/proxychains/actions/workflows/buildci.yml/badge.svg)


ProxyChains is a UNIX program, that hooks network-related libc functions
in dynamically linked programs via a preloaded DLL and redirects the
connections through SOCKS4a/5 or HTTP proxies.

WARNING: this program works only on dynamically linked programs.
also both proxychains and the program to call must use
the same dynamic linker (i.e. same libc)

## Known limitations of the current version

when a process forks, does a DNS lookup in the child, and then uses
the ip in the parent, the corresponding ip mapping will not be found,
this is because the fork can't write back into the parents mapping table.
IRSSI shows this behavior, so you have to pass the resolved ip address
to it. (you can use the proxyresolv script (requires "dig") to do so)

this means that you can't currently use tor onion urls for irssi.
to solve this issue, an external data store (file, pipe, ...) has to
manage the dns <-> ip mapping. of course there has to be proper locking.
shm_open, mkstemp, are possible candidates for a file based approach,
the other option is to spawn some kind of server process that manages the
map lookups. since connect() etc are hooked, this must not be a TCP server.

I am reluctant on doing this change, because the described behavior
seems pretty idiotic (doing a fork only for a DNS lookup), and irssi
is currently the only known affected program.

## Installation

### Using release version

*Proxychains-4.3.0* are available with pkgsrc to everyone using it on _Linux_,
_NetBSD_, _FreeBSD_, _OpenBSD_, _DragonFlyBSD_ or _Mac OS X_. You just need to install
pkgsrc-wip repository and run
  make install
in a wip/proxychains directory.

You can find out more about pkgsrc on link:http://www.pkgsrc.org[pkgsrc] and about pkgsrc-wip on
link:https://pkgsrc.org/wip[Pkgsrc-wip homepage]

### Installing on Mac OS X with homebrew

You can install current proxychains on Mac OS X with an homebrew. You have to
download unofficial link:https://gist.github.com/3792521[homebrew formula] from
to your BREW_HOME by default /usr/local/Library/Formula/ and run

```
$ brew install proxychains
```

### Running Current Source code version

```
# needs a working C compiler, preferably gcc
./configure
make
sudo make install
```

## Changelog

*Version (4.x)* removes the dnsresolver script which required a dynamically
linked "dig" binary to be present with remote DNS lookup.
this speeds up any operation involving DNS, as the old script had to use TCP.
additionally it allows to use .onion urls when used with TOR.
also it removed the broken autoconf build system with a simple Makefile.
there's a ./configure script though for convenience.
it also adds support for a config file passed via command line switches/
environment variables.

*Version (3.x)* introduces support for DNS resolving through proxy
it supports SOCKS4, SOCKS5 and HTTP CONNECT proxy servers.

* Auth-types
 ** socks - "user/pass",
 ** http - "basic"

## When to use it

* When the only way to get "outside" from your LAN is through proxy server.
* To get out from behind restrictive firewall which filters outgoing ports.
* To use two (or more) proxies in chain:

```
   like: your_host <--> proxy1 <--> proxy2 <--> target_host
```

* To "proxify" some program with no proxy support built-in (like telnet)
* Access intranet from outside via proxy.
* To use DNS behind proxy.

### Some cool features

* This program can mix different proxy types in the same chain

```
  like: your_host <-->socks5 <--> http <--> socks4 <--> target_host
```

* Different chaining options supported
  random order from the list ( user defined length of chain ).
  exact order  (as they appear in the list )
  dynamic order (smart exclude dead proxies from chain)
* You can use it with any TCP client application, even network scanners
  yes, yes - you can make portscan via proxy (or chained proxies)
  for example with Nmap scanner by fyodor (www.insecure.org/nmap).

```
  proxychains nmap -sT -PO -p 80 -iR  (find some webservers through proxy)
```

* You can use it with servers, like squid, sendmail, or whatever.
* DNS resolving through proxy.

## Configuration

proxychains looks for configuration in the following order:

* SOCKS5 proxy host ip and port in environment variable ${PROXYCHAINS_SOCKS5_HOST} ${PROXYCHAINS_SOCKS5_PORT}
  (if ${PROXYCHAINS_SOCKS5_PORT} is set, no further configuration will be searched. if ${PROXYCHAINS_SOCKS5_HOST} isn't set, host ip will become "127.0.0.1")
* file listed in environment variable ${PROXYCHAINS_CONF_FILE} or
  provided as a -f argument to proxychains script or binary.
* ./proxychains.conf
* $(HOME)/.proxychains/proxychains.conf
* /etc/proxychains.conf

see more in */etc/proxychains.conf*

### Usage Example

```
$ proxychains4 telnet targethost.com
```

in this example it will run telnet through proxy(or chained proxies)
specified by *proxychains.conf*

### Usage Example

```
$ proxychains4 -f /etc/proxychains-other.conf targethost2.com
```

in this example it will use different configuration file then *proxychains.conf*
to connect to targethost2.com host.

### Usage Example

```
$ proxyresolv targethost.com
```

in this example it will resolve targethost.com through proxy(or chained proxies)
specified by *proxychains.conf*

### Usage Example:

```
$ ssh -fN -D 4321 some.example.com
$ PROXYCHAINS_SOCKS5_HOST=127.0.0.1 PROXYCHAINS_SOCKS5_PORT=4321 proxychains zsh
```

in this example, it will run a shell with all traffic proxied through
OpenSSH's "dynamic proxy" (SOCKS5 proxy) on localhost port 4321.

### Usage Example:

```
$ export PROXY_DNS_SERVER=8.8.8.8
$ proxychains4 telnet targethost.com
```

in this example, it will telnet to targethost.com using the 8.8.8.8
nameserver supplied by the user through the PROXY_DNS_SERVER
