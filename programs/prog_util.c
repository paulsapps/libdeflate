/*
 * prog_util.c - utility functions for programs
 *
 * Copyright 2016 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "prog_util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/mman.h>
#  include <sys/time.h>
#endif

/* The invocation name of the program (filename component only) */
const tchar *program_invocation_name;

static void
do_msg(const char *format, bool with_errno, va_list va)
{
	int saved_errno = errno;

	fprintf(stderr, "%"TS": ", program_invocation_name);
	vfprintf(stderr, format, va);
	if (with_errno)
		fprintf(stderr, ": %s\n", strerror(saved_errno));
	else
		fprintf(stderr, "\n");

	errno = saved_errno;
}

/* Print a message to standard error */
void
msg(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	do_msg(format, false, va);
	va_end(va);
}

/* Print a message to standard error, including a description of errno */
void
msg_errno(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	do_msg(format, true, va);
	va_end(va);
}

/* malloc() wrapper */
void *
xmalloc(size_t size)
{
	void *p = malloc(size);
	if (p == NULL && size == 0)
		p = malloc(1);
	if (p == NULL)
		msg("Out of memory");
	return p;
}

/*
 * Retrieve the current time in nanoseconds since a start time which is fixed
 * for the duration of program execution but is otherwise unspecified
 */
u64
current_time(void)
{
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return 100 * (((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
#elif defined(HAVE_CLOCK_GETTIME)
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (1000000000 * (u64)ts.tv_sec) + ts.tv_nsec;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (1000000000 * (u64)tv.tv_sec) + (1000 * (u64)tv.tv_usec);
#endif
}

/*
 * Retrieve a pointer to the filename component of the specified path.
 *
 * Note: this does not modify the path.  Therefore, it is not guaranteed to work
 * properly for directories, since a path to a directory might have trailing
 * slashes.
 */
const tchar *
get_filename(const tchar *path)
{
	const tchar *slash = tstrrchr(path, '/');
#ifdef _WIN32
	const tchar *backslash = tstrrchr(path, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash))
		slash = backslash;
#endif
	if (slash != NULL)
		return slash + 1;
	return path;
}

/* Create a copy of 'path' surrounded by double quotes */
static tchar *
quote_path(const tchar *path)
{
	size_t len = tstrlen(path);
	tchar *result;

	result = xmalloc((1 + len + 1 + 1) * sizeof(tchar));
	if (result == NULL)
		return NULL;
	result[0] = '"';
	tmemcpy(&result[1], path, len);
	result[1 + len] = '"';
	result[1 + len + 1] = '\0';
	return result;
}

/* Open a file for reading, or set up standard input for reading */
int
xopen_for_read(const tchar *path, struct file_stream *strm)
{
	strm->mmap_mem = NULL;

	if (path == NULL) {
		strm->is_standard_stream = true;
		strm->name = T("standard input");
		strm->fd = STDIN_FILENO;
	#ifdef _WIN32
		_setmode(strm->fd, O_BINARY);
	#endif
		return 0;
	}

	strm->is_standard_stream = false;

	strm->name = quote_path(path);
	if (strm->name == NULL)
		return -1;

	strm->fd = topen(path, O_RDONLY | O_BINARY | O_NOFOLLOW);
	if (strm->fd < 0) {
		msg_errno("Can't open %"TS" for reading", strm->name);
		free(strm->name);
		return -1;
	}

	return 0;
}

/* Open a file for writing, or set up standard output for writing */
int
xopen_for_write(const tchar *path, bool overwrite, struct file_stream *strm)
{
	strm->mmap_mem = NULL;

	if (path == NULL) {
		strm->is_standard_stream = true;
		strm->name = T("standard output");
		strm->fd = STDOUT_FILENO;
	#ifdef _WIN32
		_setmode(strm->fd, O_BINARY);
	#endif
		return 0;
	}

	strm->is_standard_stream = false;

	strm->name = quote_path(path);
	if (strm->name == NULL)
		goto err;
retry:
	strm->fd = topen(path, O_WRONLY | O_BINARY | O_NOFOLLOW |
				O_CREAT | O_EXCL, 0644);
	if (strm->fd < 0) {
		if (errno != EEXIST) {
			msg_errno("Can't open %"TS" for writing", strm->name);
			goto err;
		}
		if (!overwrite) {
			if (!isatty(STDERR_FILENO) || !isatty(STDIN_FILENO)) {
				msg("%"TS" already exists; use -f to overwrite",
				    strm->name);
				goto err;
			}
			fprintf(stderr, "%"TS": %"TS" already exists; "
				"overwrite? (y/n) ",
				program_invocation_name, strm->name);
			if (getchar() != 'y') {
				msg("Not overwriting.");
				goto err;
			}
		}
		if (tunlink(path) != 0) {
			msg_errno("Unable to delete %"TS, strm->name);
			goto err;
		}
		goto retry;
	}

	return 0;

err:
	free(strm->name);
	return -1;
}

/* Map the contents of a file into memory */
int
map_file_contents(struct file_stream *strm, u64 size)
{
	if (size > SIZE_MAX) {
		msg("%"TS" is too large to be processed by this program",
		    strm->name);
		return -1;
	}
#ifdef _WIN32
	strm->mmap_token = CreateFileMapping(
				(HANDLE)(intptr_t)_get_osfhandle(strm->fd),
				NULL, PAGE_READONLY, 0, 0, NULL);
	if (strm->mmap_token == NULL) {
		msg("Unable create file mapping for %"TS": Windows error %u",
		    strm->name, (unsigned int)GetLastError());
		return -1;
	}

	strm->mmap_mem = MapViewOfFile((HANDLE)strm->mmap_token,
				       FILE_MAP_READ, 0, 0, size);
	if (strm->mmap_mem == NULL) {
		msg("Unable to map %"TS" into memory: Windows error %u",
		    strm->name, (unsigned int)GetLastError());
		CloseHandle((HANDLE)strm->mmap_token);
		return -1;
	}
#else
	strm->mmap_mem = mmap(NULL, size, PROT_READ, MAP_SHARED, strm->fd, 0);
	if (strm->mmap_mem == MAP_FAILED) {
		strm->mmap_mem = NULL;
		if (errno == ENOMEM) {
			msg("%"TS" is too large to be processed by this "
			    "program", strm->name);
		} else {
			msg_errno("Unable to map %"TS" into memory",
				  strm->name);
		}
		return -1;
	}
#endif
	strm->mmap_size = size;
	return 0;
}

/*
 * Read from a file, returning the full count to indicate all bytes were read, a
 * short count (possibly 0) to indicate EOF, or -1 to indicate error.
 */
ssize_t
xread(struct file_stream *strm, void *buf, size_t count)
{
	char *p = buf;
	size_t orig_count = count;

	while (count != 0) {
		ssize_t res = read(strm->fd, p, MIN(count, INT_MAX));
		if (res == 0)
			break;
		if (res < 0) {
			msg_errno("Error reading from %"TS, strm->name);
			return -1;
		}
		p += res;
		count -= res;
	}
	return orig_count - count;
}

/* Skip over 'count' bytes from a file, returning 0 on success or -1 on error */
int
skip_bytes(struct file_stream *strm, size_t count)
{
	size_t bufsize;
	char *buffer;
	ssize_t ret;

	if (count == 0)
		return 0;

	bufsize = MIN(count, 4096);
	buffer = xmalloc(bufsize);
	if (buffer == NULL)
		return -1;
	do {
		size_t n = MIN(count, bufsize);
		ret = xread(strm, buffer, n);
		if (ret < 0)
			goto out;
		if (ret != n) {
			msg("%"TS": unexpected end-of-file", strm->name);
			ret = -1;
			goto out;
		}
		count -= ret;
	} while (count != 0);
	ret = 0;
out:
	free(buffer);
	return ret;
}

/* Write to a file, returning 0 if all bytes were written or -1 on error */
int
full_write(struct file_stream *strm, const void *buf, size_t count)
{
	const char *p = buf;

	while (count != 0) {
		ssize_t res = write(strm->fd, p, MIN(count, INT_MAX));
		if (res <= 0) {
			msg_errno("Error writing to %"TS, strm->name);
			return -1;
		}
		p += res;
		count -= res;
	}
	return 0;
}

/* Close a file, returning 0 on success or -1 on error */
int
xclose(struct file_stream *strm)
{
	int ret = 0;
	if (strm->fd >= 0 && !strm->is_standard_stream) {
		if (close(strm->fd) != 0) {
			msg_errno("Error closing %"TS, strm->name);
			ret = -1;
		}
		free(strm->name);

		if (strm->mmap_mem != NULL) {
#ifdef _WIN32
			UnmapViewOfFile(strm->mmap_mem);
			CloseHandle((HANDLE)strm->mmap_token);
#else
			munmap(strm->mmap_mem, strm->mmap_size);
#endif
			strm->mmap_mem = NULL;
		}
	}
	strm->fd = -1;
	strm->name = NULL;
	return ret;
}

/*
 * Parse the compression level given on the command line, returning the
 * compression level on success or 0 on error
 */
int
parse_compression_level(tchar opt_char, const tchar *arg)
{
	unsigned long level = opt_char - '0';
	const tchar *p;

	if (arg == NULL)
		arg = T("");

	for (p = arg; *p >= '0' && *p <= '9'; p++)
		level = (level * 10) + (*p - '0');

	if (level < 1 || level > 12 || *p != '\0') {
		msg("Invalid compression level: \"%"TC"%"TS"\".  "
		    "Must be an integer in the range [1, 12].", opt_char, arg);
		return 0;
	}

	return level;
}

/* Allocate a new DEFLATE compressor */
struct deflate_compressor *
alloc_compressor(int level)
{
	struct deflate_compressor *c;

	c = deflate_alloc_compressor(level);
	if (c == NULL) {
		msg_errno("Unable to allocate compressor with "
			  "compression level %d", level);
	}
	return c;
}

/* Allocate a new DEFLATE decompressor */
struct deflate_decompressor *
alloc_decompressor(void)
{
	struct deflate_decompressor *d;

	d = deflate_alloc_decompressor();
	if (d == NULL)
		msg_errno("Unable to allocate decompressor");

	return d;
}
