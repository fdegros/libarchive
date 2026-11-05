/*-
 * Copyright (c) 2026 François Degros
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "test.h"

static unsigned char buff[16384];

/* Writes a tar archive of two files to buff, and returns its size. */
static size_t
make_archive(void)
{
	struct archive *a;
	struct archive_entry *ae;
	size_t used = 0;
	int i;

	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_ustar(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));
	for (i = 0; i < 2; i++) {
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_set_pathname(ae, i == 0 ? "first" : "second");
		archive_entry_set_mode(ae, AE_IFREG | 0644);
		archive_entry_set_size(ae, 5);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		assertEqualInt(5, archive_write_data(a, "hello", 5));
		archive_entry_free(ae);
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	return (used);
}

static struct archive *
open_archive(size_t size)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_tar(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff, size));
	return (a);
}

static void
assert_seek_fails(struct archive *a, la_int64_t entry_index, int err)
{
	assertEqualIntA(a, ARCHIVE_FAILED,
	    archive_read_seek_header(a, entry_index));
	assertEqualInt(err, archive_errno(a));
}

DEFINE_TEST(test_read_seek_header)
{
	struct archive *a, *w;
	struct archive_entry *ae;
	char data[16];
	size_t size = make_archive();

	/* A handle of another type is a programming error. */
	assert((w = archive_write_new()) != NULL);
	assertEqualInt(ARCHIVE_FATAL, archive_read_seek_header(w, 0));
	assertEqualInt(ARCHIVE_OK, archive_write_free(w));

	/* So is an archive that is not open yet... */
	assert((a = archive_read_new()) != NULL);
	assertEqualInt(ARCHIVE_FATAL, archive_read_seek_header(a, 0));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* ... or that is closed. */
	a = open_archive(size);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_FATAL, archive_read_seek_header(a, 0));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/*
	 * Before the first header, the format is not known yet.  A negative
	 * index is wrong whatever the format.
	 */
	a = open_archive(size);
	assert_seek_fails(a, -1, EINVAL);
	assert_seek_fails(a, 1, ENOTSUP);
	assert_seek_fails(a, 0, ENOTSUP);

	/* The failed calls did not change anything. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("first", archive_entry_pathname(ae));
	assertEqualInt(1, archive_file_count(a));

	/* Without the option, tar does not know where its headers are. */
	assert_seek_fails(a, -1, EINVAL);
	assert_seek_fails(a, 2, ENOTSUP);
	assert_seek_fails(a, 1, ENOTSUP);
	assert_seek_fails(a, 0, ENOTSUP);

	/* The entry can still be read. */
	assertEqualInt(5, archive_read_data(a, data, sizeof(data)));
	assertEqualMem(data, "hello", 5);
	assertEqualInt(1, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("second", archive_entry_pathname(ae));

	/* At the end of the archive, the call is still accepted. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_file_count(a));
	assert_seek_fails(a, 3, ENOTSUP);
	assert_seek_fails(a, 2, ENOTSUP);
	assert_seek_fails(a, 0, ENOTSUP);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/* The entries of the archives that are made by make_archive_of_four(). */
static char long_name[181];
static char big_data[1000];
static const char *names[4];
static const char *datas[4];
static const size_t sizes[4] = {5, 0, 10, 1000};

static void
init_entries(void)
{
	size_t i;

	memset(long_name, 'n', sizeof(long_name) - 1);
	memcpy(long_name, "dir/", 4);
	for (i = 0; i < sizeof(big_data); i++)
		big_data[i] = 'a' + (char)(i % 26);
	names[0] = "first";
	names[1] = "second";
	names[2] = long_name;
	names[3] = "last";
	datas[0] = "hello";
	datas[1] = "";
	datas[2] = "0123456789";
	datas[3] = big_data;
}

/*
 * Writes an archive of four entries to buff, and returns its size.  The
 * third one has a long name, that needs more headers than the others.
 * The kinds are 0: pax, 1: GNU tar, 2: gzip-compressed pax and 3: cpio.
 */
static size_t
make_archive_of_four(int kind)
{
	struct archive *a;
	struct archive_entry *ae;
	size_t used = 0;
	int i;

	init_entries();
	assert((a = archive_write_new()) != NULL);
	if (kind == 1)
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_format_gnutar(a));
	else if (kind == 3)
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_format_cpio_newc(a));
	else
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_format_pax_restricted(a));
	if (kind == 2)
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_add_filter_gzip(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));
	for (i = 0; i < 4; i++) {
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_set_pathname(ae, names[i]);
		archive_entry_set_mode(ae, AE_IFREG | 0644);
		archive_entry_set_size(ae, (la_int64_t)sizes[i]);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		if (sizes[i] > 0)
			assertEqualInt((int)sizes[i],
			    archive_write_data(a, datas[i], sizes[i]));
		archive_entry_free(ae);
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	return (used);
}

/*
 * How the locations are asked for: 0: not at all, 1: for all the formats,
 * 2: for tar only, 3: and then not any more.
 */
static struct archive *
open_with_option(size_t size, int option)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	if (option == 1 || option == 3)
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_set_options(a, "record-locations"));
	if (option == 2)
		assertEqualIntA(a, ARCHIVE_OK, archive_read_set_format_option(a,
		    "tar", "record-locations", "1"));
	if (option == 3)
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_set_options(a, "!record-locations"));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff, size));
	return (a);
}

/* Checks the entry that was just returned, and its data. */
static void
check_entry(struct archive *a, struct archive_entry *ae, int i)
{
	char data[2048];

	assertEqualString(names[i], archive_entry_pathname(ae));
	assertEqualInt(i + 1, archive_file_count(a));
	if (sizes[i] > 0) {
		assertEqualInt((int)sizes[i],
		    archive_read_data(a, data, sizeof(data)));
		assertEqualMem(data, datas[i], sizes[i]);
	}
}

/* Goes to an entry, reads it, and checks its name and data. */
static void
seek_and_check_entry(struct archive *a, int i)
{
	struct archive_entry *ae;

	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, i)))
		return;
	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae)))
		return;
	check_entry(a, ae, i);
}

/* What an entry looked like when it was read the first time. */
struct first {
	int64_t position;
	int64_t dev;
	int64_t ino;
};

static void
seek_and_check(struct archive *a, int i, const struct first *first)
{
	struct archive_entry *ae;

	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, i)))
		return;
	assertEqualInt(i, archive_file_count(a));
	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae)))
		return;
	check_entry(a, ae, i);
	assertEqualInt(first[i].dev, archive_entry_dev(ae));
	assertEqualInt(first[i].ino, archive_entry_ino(ae));
	assertEqualInt(first[i].position, archive_read_header_position(a));
}

/* Goes back to the entries of an archive of four entries. */
static void
test_kind(int kind)
{
	static const int order[] = {2, 0, 3, 1, 3, 0, 2};
	char data[16];
	struct archive *a;
	struct archive_entry *ae;
	struct first first[4];
	size_t size = make_archive_of_four(kind);
	size_t i;

	/* Read the entries, the data of every other one. */
	a = open_with_option(size, 1);
	for (i = 0; i < 4; i++) {
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));
		first[i].position = archive_read_header_position(a);
		first[i].dev = archive_entry_dev(ae);
		first[i].ino = archive_entry_ino(ae);
		if (i % 2 == 0)
			check_entry(a, ae, (int)i);
	}
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualInt(4, archive_file_count(a));

	/* From the end of the archive, go to the entries in any order. */
	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++)
		seek_and_check(a, order[i], first);

	/* From the middle of the data of an entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 3));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(10, archive_read_data(a, data, 10));
	seek_and_check(a, 0, first);

	/* The entries after the one that is read now can be reached too. */
	seek_and_check(a, 3, first);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 1));
	assertEqualInt(1, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_FAILED, archive_read_seek_header(a, -1));
	assertEqualInt(EINVAL, archive_errno(a));
	/*
	 * There is no entry after the last one: the reader is at the end of
	 * the archive, as if the last header had been read, and so is it for
	 * every index after it.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, 4));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 1));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, 5));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, 100));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* The next entry is where the reader is. */
	a = open_with_option(size, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 2));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	check_entry(a, ae, 2);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* And so is the end of the archive. */
	a = open_with_option(size, 1);
	for (i = 0; i < 4; i++)
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, 4));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Goes to entries that have not been read yet, which is going forward, then
 * back, and past the end of the archive.
 */
static void
test_forward(int kind)
{
	struct archive *a;
	struct archive_entry *ae;
	char data[16];
	size_t size = make_archive_of_four(kind);

	/*
	 * Only the first header and some of its data are read, and the third
	 * entry is gone to.
	 */
	a = open_with_option(size, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(1, archive_file_count(a));
	assertEqualInt(3, archive_read_data(a, data, 3));
	failure("kind %d", kind);
	seek_and_check_entry(a, 2);
	failure("kind %d", kind);
	seek_and_check_entry(a, 3);
	failure("kind %d", kind);
	seek_and_check_entry(a, 0);
	failure("kind %d", kind);
	seek_and_check_entry(a, 2);

	/* The end of the archive is reached by going to it directly. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, 4));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* So is it with an index that is far too large. */
	a = open_with_option(size, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_EOF,
	    archive_read_seek_header(a, (la_int64_t)1 << 40));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/*
	 * The same goes for an index after it, whether it is the first time
	 * that the end is found or not.  Then the reader can go back.
	 */
	a = open_with_option(size, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, 5));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, 6));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_EOF,
	    archive_read_seek_header(a, (la_int64_t)1 << 40));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	check_entry(a, ae, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_seek_header_tar)
{
	/* A pax archive and a GNU tar archive, which have long name headers. */
	test_kind(0);
	test_kind(1);
	test_forward(0);
	test_forward(1);
}

DEFINE_TEST(test_read_seek_header_unsupported)
{
	struct archive *a;
	struct archive_entry *ae;
	size_t size = make_archive_of_four(0);

	/* The option is not set... */
	a = open_with_option(size, 0);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assert_seek_fails(a, 0, ENOTSUP);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* ... or not any more. */
	a = open_with_option(size, 3);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assert_seek_fails(a, 0, ENOTSUP);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* The option can be set for tar only. */
	a = open_with_option(size, 2);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 0));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* A compressed archive can't seek, and can still be read. */
	if (canGzip()) {
		size = make_archive_of_four(2);
		a = open_with_option(size, 1);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));
		assert_seek_fails(a, 0, ENOTSUP);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));
		check_entry(a, ae, 1);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}

	/* A format that can't seek to a header. */
	size = make_archive_of_four(3);
	a = open_with_option(size, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assert_seek_fails(a, 0, ENOTSUP);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

#define MAX_SEEN 64

static const char *current_file;

/* What an entry looks like when the archive is read from the start. */
struct seen {
	char *name;
	int mode;
	int64_t dev;
	int64_t ino;
	int header_result;
	int64_t bytes;
	unsigned hash;
	int data_result;
};

/* Some entries have no name. */
static const char *
name_of(struct archive_entry *ae)
{
	const char *name = archive_entry_pathname(ae);

	return (name != NULL ? name : "");
}

/* Reads the data of an entry, and tells how much, what, and how it ended. */
static void
digest_entry(struct archive *a, struct seen *seen)
{
	char buf[4096];
	la_ssize_t n;
	size_t i;

	seen->bytes = 0;
	seen->hash = 2166136261u;
	while ((n = archive_read_data(a, buf, sizeof(buf))) > 0) {
		for (i = 0; i < (size_t)n; i++)
			seen->hash = (seen->hash ^ (unsigned char)buf[i]) *
			    16777619u;
		seen->bytes += n;
	}
	seen->data_result = (int)n;
}

static void
seek_and_compare(struct archive *a, int i, const struct seen *want)
{
	struct archive_entry *ae;
	struct seen got;

	failure("%s, entry %d", current_file, i);
	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, i)))
		return;
	failure("%s, entry %d", current_file, i);
	if (!assertEqualIntA(a, want[i].header_result,
	    archive_read_next_header(a, &ae)))
		return;
	failure("%s, entry %d", current_file, i);
	assertEqualInt(i + 1, archive_file_count(a));
	failure("%s, entry %d", current_file, i);
	assertEqualString(want[i].name, name_of(ae));
	failure("%s, entry %d", current_file, i);
	assertEqualInt(want[i].mode, archive_entry_mode(ae));
	failure("%s, entry %d", current_file, i);
	assertEqualInt(want[i].dev, archive_entry_dev(ae));
	failure("%s, entry %d", current_file, i);
	assertEqualInt(want[i].ino, archive_entry_ino(ae));
	if (want[i].header_result != ARCHIVE_OK &&
	    want[i].header_result != ARCHIVE_WARN)
		return;
	digest_entry(a, &got);
	failure("%s, entry %d", current_file, i);
	assertEqualInt(want[i].bytes, got.bytes);
	failure("%s, entry %d", current_file, i);
	assertEqualInt(want[i].hash, got.hash);
	failure("%s, entry %d", current_file, i);
	assertEqualInt(want[i].data_result, got.data_result);
}

static struct archive *
open_reference(const char *name)
{
	struct archive *a;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_options(a, "record-locations"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, name, 10240));
	return (a);
}

/*
 * Goes to the entries of a file in the opposite order of the one they are
 * read in, and then in the same order, and checks that they are the same as
 * when they were read the first time.  Then another reader that has read
 * the first header only goes to the entries that it has not read.
 */
static void
test_file(const char *name)
{
	struct seen seen[MAX_SEEN];
	struct archive *a;
	struct archive_entry *ae;
	int n = 0, i, result;

	current_file = name;
	extract_reference_file(name);
	a = open_reference(name);

	/* Read the entries from the start. */
	while (n < MAX_SEEN) {
		result = archive_read_next_header(a, &ae);
		if (result == ARCHIVE_EOF || result == ARCHIVE_FATAL)
			break;
		seen[n].header_result = result;
		seen[n].name = strdup(name_of(ae));
		seen[n].mode = archive_entry_mode(ae);
		seen[n].dev = archive_entry_dev(ae);
		seen[n].ino = archive_entry_ino(ae);
		seen[n].bytes = 0;
		seen[n].hash = 0;
		seen[n].data_result = 0;
		if (result == ARCHIVE_OK || result == ARCHIVE_WARN)
			digest_entry(a, &seen[n]);
		n++;
	}
	failure("%s", name);
	assertEqualIntA(a, ARCHIVE_EOF, result);
	assert(n > 0);

	for (i = n - 1; i >= 0; i--)
		seek_and_compare(a, i, seen);
	for (i = 0; i < n; i++)
		seek_and_compare(a, i, seen);

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Go forward, to the last entry, the middle one, and back. */
	a = open_reference(name);
	assertEqualIntA(a, seen[0].header_result,
	    archive_read_next_header(a, &ae));
	seek_and_compare(a, n - 1, seen);
	seek_and_compare(a, n / 2, seen);
	seek_and_compare(a, 0, seen);
	seek_and_compare(a, n - 1, seen);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	for (i = 0; i < n; i++)
		free(seen[i].name);
}

DEFINE_TEST(test_read_seek_header_tar_files)
{
	static const char *refs[] = {
		"test_read_format_gtar_sparse_1_13.tar",
		"test_read_format_gtar_sparse_1_17.tar",
		"test_read_format_gtar_sparse_1_17_posix00.tar",
		"test_read_format_gtar_sparse_1_17_posix01.tar",
		"test_read_format_gtar_sparse_1_17_posix10.tar",
		"test_read_format_gtar_sparse_1_17_posix10_modified.tar",
		"test_read_format_gtar_sparse_reuse.tar",
		"test_read_format_tar_pax_sun_holesdata.tar",
		"test_read_format_tar_pax_global_header_ignorable.tar",
		"test_read_format_tar_pax_timestamps.tar",
		"test_read_format_tar_pax_negative_time.tar",
		"test_read_format_tar_mac_metadata_1.tar",
		"test_read_format_tar_concatenated.tar",
		"test_compat_gtar_1.tar",
		"test_compat_gtar_2.tar",
		"test_compat_perl_archive_tar.tar",
		"test_compat_plexus_archiver_tar.tar",
		"test_compat_tar_directory_1.tar",
		"test_compat_tar_hardlink_1.tar",
		"test_pax_filename_encoding.tar",
		"test_pax_xattr_header_all.tar",
		"test_acl_pax_posix1e.tar",
	};
	size_t r;

	for (r = 0; r < sizeof(refs) / sizeof(refs[0]); r++)
		test_file(refs[r]);
}

/* More entries than the locations are first made room for. */
#define MANY 200

static void
many_name(char *name, size_t size, int i)
{
	snprintf(name, size, "dir/file%03d", i);
}

static void
make_many(const char *filename)
{
	struct archive *a;
	struct archive_entry *ae;
	char name[32], data[32];
	int i, length;

	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_format_pax_restricted(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_filename(a, filename));
	for (i = 0; i < MANY; i++) {
		many_name(name, sizeof(name), i);
		length = snprintf(data, sizeof(data), "contents of %d", i);
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_set_pathname(ae, name);
		archive_entry_set_mode(ae, AE_IFREG | 0644);
		archive_entry_set_size(ae, length);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		assertEqualInt(length, archive_write_data(a, data, length));
		archive_entry_free(ae);
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
}

static void
check_many(struct archive *a, struct archive_entry *ae, int i)
{
	char name[32], data[32], buf[64];
	int length = snprintf(data, sizeof(data), "contents of %d", i);

	many_name(name, sizeof(name), i);
	failure("entry %d", i);
	assertEqualString(name, archive_entry_pathname(ae));
	failure("entry %d", i);
	assertEqualInt(i + 1, archive_file_count(a));
	failure("entry %d", i);
	assertEqualInt(length, archive_read_data(a, buf, sizeof(buf)));
	assertEqualMem(buf, data, length);
}

static void
go_to_many(struct archive *a, int i)
{
	struct archive_entry *ae;

	failure("entry %d", i);
	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, i)))
		return;
	failure("entry %d", i);
	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae)))
		return;
	check_many(a, ae, i);
}

/* Reads all the entries, and then goes to them in several orders. */
static void
read_and_go_back(struct archive *a)
{
	struct archive_entry *ae;
	int i;

	/*
	 * Go to entries that have not been read, far in the archive, and in
	 * the middle of it, and past its end, before anything else.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	go_to_many(a, 150);
	go_to_many(a, MANY - 1);
	go_to_many(a, 20);
	go_to_many(a, 21);
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, MANY + 1));
	assertEqualInt(MANY, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 0));

	for (i = 0; i < MANY; i++) {
		if (!assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae)))
			return;
		check_many(a, ae, i);
	}
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualInt(MANY, archive_file_count(a));

	for (i = MANY - 1; i >= 0; i--)
		go_to_many(a, i);
	/* 71 and 200 have nothing in common: all the entries, scrambled. */
	for (i = 0; i < MANY; i++)
		go_to_many(a, (i * 71) % MANY);

	/* The end of the archive, and nothing after it. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, MANY));
	assertEqualInt(MANY, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_seek_header(a, MANY + 1));
	assertEqualInt(MANY, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, MANY - 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	check_many(a, ae, MANY - 1);
}

DEFINE_TEST(test_read_seek_header_many)
{
	static const char *parts[] = {
	    "many.tar.aa", "many.tar.ab", "many.tar.ac", NULL};
	struct archive *a;
	char *contents;
	size_t size, start, length;
	int i;

	/* A tar file, read from the file, not from memory. */
	make_many("many.tar");
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_options(a, "record-locations"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, "many.tar", 10240));
	read_and_go_back(a);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* The same tar file in three pieces, cut in the middle of entries. */
	contents = slurpfile(&size, "many.tar");
	assert(contents != NULL);
	for (i = 0; i < 3; i++) {
		FILE *f;

		start = size / 3 * i + (i > 0 ? 100 : 0);
		length = (i == 2 ? size : size / 3 * (i + 1) + 100) - start;
		assert((f = fopen(parts[i], "wb")) != NULL);
		assertEqualInt(length, fwrite(contents + start, 1, length, f));
		assertEqualInt(0, fclose(f));
	}
	free(contents);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_options(a, "record-locations"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filenames(a, parts, 10240));
	read_and_go_back(a);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/* A source of the archive in buff, whose seek can be made to fail. */
struct failing_source {
	size_t size;
	size_t position;
	int seeks;	/* The seeks since fail_on was set. */
	int fail_on;	/* The seek that fails, starting from 1, or 0. */
};

static ssize_t
failing_read(struct archive *a, void *data, const void **buffer)
{
	struct failing_source *source = data;
	size_t length = source->size - source->position;

	(void)a; /* UNUSED */
	if (length > 4096)
		length = 4096;
	*buffer = buff + source->position;
	source->position += length;
	return ((ssize_t)length);
}

static int64_t
failing_skip(struct archive *a, void *data, int64_t request)
{
	struct failing_source *source = data;

	(void)a; /* UNUSED */
	source->position += (size_t)request;
	return (request);
}

static int64_t
failing_seek(struct archive *a, void *data, int64_t offset, int whence)
{
	struct failing_source *source = data;
	int64_t position;

	(void)a; /* UNUSED */
	if (whence == SEEK_SET)
		position = offset;
	else if (whence == SEEK_CUR)
		position = (int64_t)source->position + offset;
	else
		position = (int64_t)source->size + offset;
	if (source->fail_on != 0 && ++source->seeks == source->fail_on)
		return (ARCHIVE_FAILED);
	source->position = (size_t)position;
	return (position);
}

/* Opens the tar archive in buff, and reads its first header again. */
static struct archive *
open_failing(struct failing_source *source, size_t size)
{
	struct archive_entry *ae;
	struct archive *a;

	memset(source, 0, sizeof(*source));
	source->size = size;
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_tar(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_options(a, "record-locations"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_read_callback(a, failing_read));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_skip_callback(a, failing_skip));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_seek_callback(a, failing_seek));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_callback_data(a, source));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open1(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));

	/*
	 * The first seek in a source also asks for its size, which is not
	 * what is being tried, so do one before.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_seek_header(a, 0));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(1, archive_file_count(a));
	return (a);
}

DEFINE_TEST(test_read_seek_header_seek_failure)
{
	struct failing_source source;
	struct archive_entry *ae;
	struct archive *a;
	size_t size = make_archive_of_four(0);

	/*
	 * Going forward to an entry that has not been read, the seek that
	 * starts to look for it fails: the reader has not moved.
	 */
	a = open_failing(&source, size);
	source.fail_on = 1;
	assertEqualIntA(a, ARCHIVE_FAILED, archive_read_seek_header(a, 3));
	source.fail_on = 0;
	assertEqualInt(1, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	check_entry(a, ae, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/*
	 * The seek that goes to the entry fails after the reader has moved
	 * to find it: it cannot be put back, and must not go on from there.
	 */
	a = open_failing(&source, size);
	source.fail_on = 2;
	assertEqualIntA(a, ARCHIVE_FATAL, archive_read_seek_header(a, 3));
	source.fail_on = 0;
	assertEqualIntA(a, ARCHIVE_FATAL, archive_read_next_header(a, &ae));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Going back to an entry that has been read, the only seek fails. */
	a = open_failing(&source, size);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(3, archive_file_count(a));
	source.fail_on = 1;
	assertEqualIntA(a, ARCHIVE_FAILED, archive_read_seek_header(a, 1));
	source.fail_on = 0;
	assertEqualInt(3, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	check_entry(a, ae, 3);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}
