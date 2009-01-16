#define _GNU_SOURCE
#include "tup/access_event.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dlfcn.h>

static void handle_file(const char *file, int at);
static void handle_rename_file(const char *old, const char *new);
static int ignore_file(const char *file);
static void ldpre_init(void) __attribute__((constructor));
static int sd;
static struct sockaddr_un addr;
static int my_pid;

static int (*s_open)(const char *, int, ...);
static int (*s_open64)(const char *, int, ...);
static FILE *(*s_fopen)(const char *, const char *);
static FILE *(*s_fopen64)(const char *, const char *);
static FILE *(*s_freopen)(const char *, const char *, FILE *);
static int (*s_creat)(const char *, mode_t);
static int (*s_rename)(const char*, const char*);
static int (*s_unlink)(const char*);
static int (*s_unlinkat)(int, const char*, int);

int open(const char *pathname, int flags, ...)
{
	int rc;

	mode_t mode = 0;
	if(flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	/* O_ACCMODE is 0x3, which covers O_WRONLY and O_RDWR */
	rc = s_open(pathname, flags, mode);
	if(rc >= 0)
		handle_file(pathname, flags&O_ACCMODE);
	return rc;
}

int open64(const char *pathname, int flags, ...)
{
	int rc;

	mode_t mode = 0;
	if(flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	rc = s_open64(pathname, flags, mode);
	if(rc >= 0)
		handle_file(pathname, flags&O_ACCMODE);
	return rc;
}

FILE *fopen(const char *path, const char *mode)
{
	FILE *f;

	f = s_fopen(path, mode);
	if(f)
		handle_file(path, !(mode[0] == 'r'));
	return f;
}

FILE *fopen64(const char *path, const char *mode)
{
	FILE *f;

	f = s_fopen64(path, mode);
	if(f)
		handle_file(path, !(mode[0] == 'r'));
	return f;
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
	FILE *f;

	f = s_freopen(path, mode, stream);
	if(f)
		handle_file(path, !(mode[0] == 'r'));
	return f;
}

int creat(const char *pathname, mode_t mode)
{
	int rc;

	rc = s_creat(pathname, mode);
	if(rc >= 0)
		handle_file(pathname, 1);
	return rc;
}

int rename(const char *old, const char *new)
{
	int rc;

	rc = s_rename(old, new);
	if(rc == 0) {
		handle_rename_file(old, new);
	}
	return rc;
}

int unlink(const char *pathname)
{
	int rc;

	rc = s_unlink(pathname);
	if(rc == 0) {
		handle_file(pathname, ACCESS_UNLINK);
	}
	return rc;
}

int unlinkat(int dirfd, const char *pathname, int flags)
{
	int rc;

	rc = s_unlinkat(dirfd, pathname, flags);
	if(rc == 0) {
		if(dirfd == AT_FDCWD) {
			handle_file(pathname, ACCESS_UNLINK);
		} else {
			fprintf(stderr, "tup.ldpreload: Error - unlinkat() not supported unless dirfd == AT_FDCWD\n");
			return -1;
		}
	}
	return rc;
}

static void handle_file(const char *file, int at)
{
	struct access_event event;

	if(ignore_file(file))
		return;

	event.at = at;
	event.pid = my_pid;
	event.len = strlen(file) + 1;
	sendto(sd, &event, sizeof(event), MSG_MORE, (void*)&addr, sizeof(addr));
	sendto(sd, file, event.len, 0, (void*)&addr, sizeof(addr));
}

static void handle_rename_file(const char *old, const char *new)
{
	if(ignore_file(old) || ignore_file(new))
		return;

	handle_file(old, ACCESS_RENAME_FROM);
	handle_file(new, ACCESS_RENAME_TO);
}

static int ignore_file(const char *file)
{
	if(strncmp(file, "/tmp/", 5) == 0)
		return 1;
	if(strncmp(file, "/dev/", 5) == 0)
		return 1;
	if(strstr(file, ".tup") != NULL)
		return 1;
	return 0;
}

static void ldpre_init(void)
{
	char *path;
	s_open = dlsym(RTLD_NEXT, "open");
	s_open64 = dlsym(RTLD_NEXT, "open64");
	s_fopen = dlsym(RTLD_NEXT, "fopen");
	s_fopen64 = dlsym(RTLD_NEXT, "fopen64");
	s_freopen = dlsym(RTLD_NEXT, "freopen");
	s_creat = dlsym(RTLD_NEXT, "creat");
	s_rename = dlsym(RTLD_NEXT, "rename");
	s_unlink = dlsym(RTLD_NEXT, "unlink");
	s_unlinkat = dlsym(RTLD_NEXT, "unlinkat");
	if(!s_open || !s_open64 || !s_fopen || !s_fopen64 || !s_freopen ||
	   !s_creat || !s_rename || !s_unlink || !s_unlinkat) {
		fprintf(stderr, "tup.ldpreload: Unable to get real symbols!\n");
		exit(1);
	}

	my_pid = getpid();

	path = getenv(SERVER_NAME);
	if(!path) {
		fprintf(stderr, "tup.ldpreload: Unable to get '%s' "
			"path from the environment.\n", SERVER_NAME);
		exit(1);
	}
	strncpy(addr.sun_path, path, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = 0;
	addr.sun_family = AF_UNIX;

	sd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if(sd < 0) {
		perror("tup.ldpreload: socket");
		exit(1);
	}
}
