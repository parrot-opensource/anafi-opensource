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

/* test plugin, implementing a fake iio device tree for testing purpose */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <argz.h>
#include <envz.h>

#include <assert.h>

#include "iio-private.h"
#include "debug.h"

#define CONTEXT_NAME "test"
#define TEST_PLUGIN_DESCRIPTION CONTEXT_NAME" plugin context"
#define TEST_PLUGIN_DTD "<!DOCTYPE context [<!ELEMENT context (device)*>\
<!ELEMENT device (channel | attribute | debug-attribute)*>\
<!ELEMENT channel (scan-element?, attribute*)>\
<!ELEMENT attribute EMPTY><!ELEMENT scan-element EMPTY>\
<!ELEMENT debug-attribute EMPTY>\
<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>\
<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>\
<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name \
	CDATA #IMPLIED>\
<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale \
	CDATA #IMPLIED>\
<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>\
<!ATTLIST debug-attribute name CDATA #REQUIRED>]>"

#define TEST_PLUGIN_XML_PATTERN "<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
		TEST_PLUGIN_DTD \
		"<context name=\""CONTEXT_NAME"\" description=\"" \
		TEST_PLUGIN_DESCRIPTION"\">" \
		"%s" \
		"</context>"

struct iio_context_pdata {

};

struct iio_device_pdata {

};

static struct iio_context * test_create_context(void);

static struct iio_context_factory test_factory = {
		.name = CONTEXT_NAME,
		.create_context = test_create_context,
};

static __attribute__((constructor)) void test_init(void)
{
	int ret;

	ret = iio_context_factory_register(&test_factory);
	if (ret < 0)
		ERROR("iio_context_factory_register: %s\n", strerror(-ret));
}

static __attribute__((destructor)) void test_cleanup(void)
{
	int ret;

	ret = iio_context_factory_unregister(CONTEXT_NAME);
	if (ret < 0)
		ERROR("iio_context_factory_unregister: %s\n", strerror(-ret));
}

static int test_open(const struct iio_device *dev, size_t samples_count,
		bool cyclic)
{

	return -ENOSYS;
}

static int test_close(const struct iio_device *dev)
{

	return -ENOSYS;
}

static ssize_t test_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	errno = ENOSYS;

	return -1;
}

static ssize_t test_write(const struct iio_device *dev, const void *src,
		size_t len)
{
	errno = ENOSYS;

	return -1;
}

static ssize_t test_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug)
{
	errno = ENOSYS;

	return -1;
}

static ssize_t test_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, bool is_debug)
{
	errno = ENOSYS;

	return -1;
}

static ssize_t test_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	errno = ENOSYS;

	return -1;
}

static ssize_t test_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	errno = ENOSYS;

	return -1;
}

static int test_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{

	return -ENOSYS;
}

static int test_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{

	return -ENOSYS;
}

static void test_shutdown(struct iio_context *ctx)
{
	errno = ENOSYS;

	return;
}

static int test_get_version(const struct iio_context *ctx, unsigned int *major,
		unsigned int *minor, char git_tag[8])
{

	return -ENOSYS;
}

static int test_set_timeout(struct iio_context *ctx, unsigned int timeout)
{

	return -ENOSYS;
}

static int test_get_timeout(struct iio_context *ctx, unsigned int *timeout)
{

	return -ENOSYS;
}

static struct iio_context * test_clone(const struct iio_context *ctx)
{
	return test_create_context();
}

static int test_set_blocking_mode(const struct iio_device *dev, bool blocking)
{

	return -ENOSYS;
}

static struct iio_backend_ops test_ops = {
		.clone = test_clone,
		.open = test_open,
		.close = test_close,
		.set_blocking_mode = test_set_blocking_mode,
		.read = test_read,
		.write = test_write,
#ifdef WITH_NETWORK_GET_BUFFER
		.get_buffer = test_get_buffer,
#endif
		.read_device_attr = test_read_dev_attr,
		.write_device_attr = test_write_dev_attr,
		.read_channel_attr = test_read_chn_attr,
		.write_channel_attr = test_write_chn_attr,
		.get_trigger = test_get_trigger,
		.set_trigger = test_set_trigger,
		.shutdown = test_shutdown,
		.get_version = test_get_version,
		.set_timeout = test_set_timeout,
		.get_timeout = test_get_timeout,
};

static struct iio_context * test_create_context(void)
{
	int ret;
	char *xml = NULL;
	struct iio_context *ctx;
	int xml_len;
	const char *entry;

	xml_len = asprintf(&xml, TEST_PLUGIN_XML_PATTERN, " ");
	if (xml_len < 0) {
		ret = ENOMEM;
		return NULL;
	}
	ctx = iio_create_xml_context_mem(xml, xml_len);
	if (ctx == NULL) {
		free(xml);
		return NULL;
	}
	ctx->xml = xml;
	ctx->ops = &test_ops;

	entry = envz_get(test_factory.envz, test_factory.envz_len, "key1");
	assert(strcmp(entry, "value1") == 0);
	entry = envz_get(test_factory.envz, test_factory.envz_len, "key2");
	assert(strcmp(entry, "value2") == 0);

	return ctx;
}
