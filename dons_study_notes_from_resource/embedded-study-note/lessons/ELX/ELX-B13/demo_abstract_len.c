/* ENV: linux */
/* ELX-B13 demo: an abstract AF_UNIX name is "exactly addrlen bytes" — the classic sd_notify bug.
 * The receiver binds "@esn-b13-demo" with the exact length; the sender tries three lengths.
 * build/run (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_abstract_len.c -o /tmp/ELX-B13_demo && /tmp/ELX-B13_demo
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void try_len(const char *what, const struct sockaddr_un *sa, socklen_t len)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	ssize_t n = sendto(fd, "READY=1", 7, MSG_NOSIGNAL, (const struct sockaddr *)sa, len);
	printf("%-36s len=%3u -> %s\n", what, (unsigned)len, n < 0 ? strerror(errno) : "delivered");
	close(fd);
}

int main(void)
{
	const char *name = "esn-b13-demo";
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path + 1, name, strlen(name)); /* sun_path[0] = '\0' -> abstract */
	socklen_t exact = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name));
	int rx = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (bind(rx, (struct sockaddr *)&sa, exact) < 0) {
		perror("bind");
		return 1;
	}
	try_len("exact (offsetof + 1 + strlen)", &sa, exact);
	try_len("exact + 1 (a trailing NUL)", &sa, exact + 1);
	try_len("sizeof(struct sockaddr_un)", &sa, sizeof sa);
	char buf[32];
	ssize_t got = recv(rx, buf, sizeof buf, MSG_DONTWAIT);
	printf("receiver got %zd bytes\n", got);
	close(rx);
	return 0;
}
