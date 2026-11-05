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

	/* No format can go to a header yet. */
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

	/* At the end of the archive, the call is still checked the same. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_file_count(a));
	assert_seek_fails(a, 3, ENOTSUP);
	assert_seek_fails(a, 2, ENOTSUP);
	assert_seek_fails(a, 0, ENOTSUP);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}
