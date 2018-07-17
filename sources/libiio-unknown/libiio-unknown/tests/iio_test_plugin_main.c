/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

/* program running automated tests, using the test plugin */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <argz.h>
#include <envz.h>

#include <iio.h>

/* only used for STRINGIFY implementation */
#define STRINGIFY_HELPER(s) #s

/* transforms it's argument to a valid string */
#define STRINGIFY(s) STRINGIFY_HELPER(s)

static void str_free(char **str)
{
	if (!str || !*str)
		return;

	free(*str);

	*str = NULL;
}

#define REGISTER_TEST(t) { \
		.name = STRINGIFY(t), \
		.run = t##_TEST, \
},

static void iio_create_context_TEST(void)
{
	struct iio_context *ctx;
	char __attribute__((cleanup(str_free)))*envz = NULL;
	size_t envz_len = 0;
	error_t err;

	err = argz_create((char *[]){"key1=value1", "key2=value2", NULL}, &envz,
			&envz_len);
	assert(err == 0);
	ctx = iio_create_context("test", envz, envz_len);
	assert(ctx);

	iio_context_destroy(ctx);

	/* error cases */
	ctx = iio_create_context("test", envz, 0);
	assert(!ctx);
	ctx = iio_create_context("", envz, envz_len);
	assert(!ctx);
	ctx = iio_create_context(NULL, envz, envz_len);
	assert(!ctx);
}

static void iio_create_default_context_TEST(void)
{
	struct iio_context *ctx;
	int ret;

	ret = setenv("IIO_BACKEND_PLUGIN", "test:key1=value1:key2=value2",
			true);
	assert(ret == 0);
	ctx = iio_create_default_context();
	assert(ctx);

	iio_context_destroy(ctx);

	/* no error cases */
}

struct {
	const char *name;
	void (*run)(void);
} tests[] = {
		REGISTER_TEST(iio_create_context)
		REGISTER_TEST(iio_create_default_context)
		{NULL} /* NULL guard */
};

static void run_test(int i)
{
	fprintf(stderr, "***** test[%d] (%s)... ", i, tests[i].name);
	tests[i].run();
	fprintf(stderr, "OK\n");
}

int main(void)
{
	int i;

	for (i = 0; tests[i].name; i++)
		run_test(i);

	return EXIT_SUCCESS;
}
