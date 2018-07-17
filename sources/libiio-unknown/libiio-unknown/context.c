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

#include "debug.h"
#include "iio-config.h"
#include "iio-private.h"

#include <errno.h>
#include <string.h>
#include <argz.h>
#include <envz.h>

#ifdef _WIN32
#define LOCAL_BACKEND 0
#define NETWORK_BACKEND 1
#endif

#ifndef MAX_FACTORIES
#define MAX_FACTORIES 10
#endif /* MAX_FACTORIES */

#ifndef IIO_BACKEND_PLUGIN_ENV_VAR
#define IIO_BACKEND_PLUGIN_ENV_VAR "IIO_BACKEND_PLUGIN"
#endif /* IIO_BACKEND_PLUGIN_ENV_VAR */

#ifndef IIO_PLUGIN_DESC_SEPARATOR
#define IIO_PLUGIN_DESC_SEPARATOR ':'
#endif /* IIO_PLUGIN_DESC_SEPARATOR */

static struct iio_context_factory *context_factories[MAX_FACTORIES];
static unsigned factories_nb;

static int factory_set_properties(struct iio_context_factory *factory,
		const char *envz, size_t envz_len)
{
	if (!factory|| !envz || !*envz || envz_len == 0)
		return -EINVAL;

	factory->envz = realloc(factory->envz, envz_len);
	if (!factory->envz)
		return -errno;

	memcpy(factory->envz, envz, envz_len);
	factory->envz_len = envz_len;

	return 0;
}

static struct iio_context_factory * get_context_factory(const char *name)
{
	unsigned i;

	iio_init_plugins();
	for (i = 0; i < MAX_FACTORIES; i++)
		if (context_factories[i] &&
				!strcmp(context_factories[i]->name, name))
			return context_factories[i];

	errno = ENOSYS;

	return NULL;
}

struct iio_context * iio_create_context(const char *name, const char *envz,
		size_t envz_len)
{
	int ret;
	struct iio_context_factory *factory;

	if (!name|| !*name) {
		errno = -EINVAL;
		return NULL;
	}

	factory = get_context_factory(name);
	if (!factory) {
		ERROR("Cannot find factory called '%s'.\n"
				"That can be due to missing plugins.\n", name);
		return NULL;
    }

	if (envz) {
		ret = factory_set_properties(factory, envz, envz_len);
		if (ret < 0) {
			errno = -ret;
			return NULL;
		}
	}

	return factory->create_context();
}

int iio_context_factory_register(struct iio_context_factory *factory)
{
	if (!factory || !factory->create_context || !factory->name)
		return -EINVAL;

	DEBUG("%s \"%s\"\n", __func__, factory->name);

	if (factories_nb == MAX_FACTORIES)
		return -ENOMEM;
	context_factories[factories_nb] = factory;
	factories_nb++;

	return 0;
}

static void cleanup_factory(struct iio_context_factory *factory)
{
	free(factory->envz);
	factory->envz = NULL;
	factory->envz_len = 0;
}

int iio_context_factory_unregister(const char *name)
{
	unsigned i;

	if (!name)
		return -EINVAL;

	DEBUG("%s \"%s\"\n", __func__, name);

	for (i = 0; i < MAX_FACTORIES; i++)
		if (!strcmp(context_factories[i]->name, name))
			break;
	if (i == MAX_FACTORIES)
		return -ENOENT;

	if (context_factories[i]->envz)
		cleanup_factory(context_factories[i]);

	factories_nb--;
	for (; i + 1 < MAX_FACTORIES; i++)
		context_factories[i] = context_factories[i + 1];
	memset(context_factories + i, 0, sizeof(*context_factories));

	return 0;
}

const char * iio_context_factory_get_property(
		const struct iio_context_factory *factory, const char *key)
{
	const char *value;

	if (!factory || !key || !*key) {
		errno = EINVAL;
		return NULL;
	}

	value = envz_get(factory->envz, factory->envz_len, key);

	if (value)
		return value;

	errno = ENOENT;

	return NULL;
}

void iio_context_dump_factories(void)
{
	unsigned i;

	DEBUG("List of registered context factories:\n");

	for (i = 0; i < factories_nb; i++)
		DEBUG("\t\"%s\" %p\n", context_factories[i]->name,
				context_factories[i]->create_context);
}

static const char xml_header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<!DOCTYPE context ["
"<!ELEMENT context (device | context-attribute)*>"
"<!ELEMENT context-attribute EMPTY>"
"<!ELEMENT device (channel | attribute | debug-attribute)*>"
"<!ELEMENT channel (scan-element?, attribute*)>"
"<!ELEMENT attribute EMPTY>"
"<!ELEMENT scan-element EMPTY>"
"<!ELEMENT debug-attribute EMPTY>"
"<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>"
"<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>"
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>"
"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>"
"<!ATTLIST debug-attribute name CDATA #REQUIRED>"
"]>";

/* Returns a string containing the XML representation of this context */
char * iio_context_create_xml(const struct iio_context *ctx)
{
	size_t len, *devices_len = NULL;
	char *str, *ptr, **devices = NULL;
	unsigned int i;

	len = strlen(ctx->name) + sizeof(xml_header) - 1 +
		sizeof("<context name=\"\" ></context>");
	if (ctx->description)
		len += strlen(ctx->description) +
			sizeof(" description=\"\"") - 1;

	for (i = 0; i < ctx->nb_attrs; i++)
		len += strlen(ctx->attrs[i]) +
			strlen(ctx->values[i]) +
			sizeof("<context-attribute name=\"\" value=\"\" />");

	if (ctx->nb_devices) {
		devices_len = malloc(ctx->nb_devices * sizeof(*devices_len));
		if (!devices_len) {
			errno = ENOMEM;
			return NULL;
		}

		devices = calloc(ctx->nb_devices, sizeof(*devices));
		if (!devices)
			goto err_free_devices_len;

		for (i = 0; i < ctx->nb_devices; i++) {
			char *xml = iio_device_get_xml(ctx->devices[i],
					&devices_len[i]);
			if (!xml)
				goto err_free_devices;
			devices[i] = xml;
			len += devices_len[i];
		}
	}

	str = malloc(len);
	if (!str) {
		errno = ENOMEM;
		goto err_free_devices;
	}

	if (ctx->description) {
		iio_snprintf(str, len, "%s<context name=\"%s\" "
				"description=\"%s\" >",
				xml_header, ctx->name, ctx->description);
	} else {
		iio_snprintf(str, len, "%s<context name=\"%s\" >",
				xml_header, ctx->name);
	}

	ptr = strrchr(str, '\0');

	for (i = 0; i < ctx->nb_attrs; i++)
		ptr += sprintf(ptr, "<context-attribute name=\"%s\" value=\"%s\" />",
				ctx->attrs[i], ctx->values[i]);


	for (i = 0; i < ctx->nb_devices; i++) {
		strcpy(ptr, devices[i]);
		ptr += devices_len[i];
		free(devices[i]);
	}

	free(devices);
	free(devices_len);
	strcpy(ptr, "</context>");
	return str;

err_free_devices:
	for (i = 0; i < ctx->nb_devices; i++)
		free(devices[i]);
	free(devices);
err_free_devices_len:
	free(devices_len);
	return NULL;
}

const char * iio_context_get_xml(const struct iio_context *ctx)
{
	return ctx->xml;
}

const char * iio_context_get_name(const struct iio_context *ctx)
{
	return ctx->name;
}

const char * iio_context_get_description(const struct iio_context *ctx)
{
	if (ctx->description)
		return ctx->description;
	else
		return "";
}

void iio_context_destroy(struct iio_context *ctx)
{
	unsigned int i;
	if (ctx->ops->shutdown)
		ctx->ops->shutdown(ctx);

	for (i = 0; i < ctx->nb_attrs; i++) {
		free(ctx->attrs[i]);
		free(ctx->values[i]);
	}
	if (ctx->nb_attrs) {
		free(ctx->attrs);
		free(ctx->values);
	}
	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
	if (ctx->xml)
		free(ctx->xml);
	if (ctx->description)
		free(ctx->description);
	free(ctx);
}

unsigned int iio_context_get_devices_count(const struct iio_context *ctx)
{
	return ctx->nb_devices;
}

struct iio_device * iio_context_get_device(const struct iio_context *ctx,
		unsigned int index)
{
	if (index >= ctx->nb_devices)
		return NULL;
	else
		return ctx->devices[index];
}

struct iio_device * iio_context_find_device(const struct iio_context *ctx,
		const char *name)
{
	unsigned int i;
	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		if (!strcmp(dev->id, name) ||
				(dev->name && !strcmp(dev->name, name)))
			return dev;
	}
	return NULL;
}

static void reorder_channels(struct iio_device *dev)
{
	bool found;
	unsigned int i;

	/* Reorder channels by index */
	do {
		found = false;
		for (i = 1; i < dev->nb_channels; i++) {
			struct iio_channel **channels = dev->channels;
			long ch1 = channels[i - 1]->index;
			long ch2 = channels[i]->index;

			if (ch1 == ch2 && ch1 >= 0) {
				ch1 = channels[i - 1]->format.shift;
				ch2 = channels[i]->format.shift;
			}

			if (ch2 >= 0 && ((ch1 > ch2) || ch1 < 0)) {
				struct iio_channel *bak = channels[i];
				channels[i] = channels[i - 1];
				channels[i - 1] = bak;
				found = true;
			}
		}
	} while (found);
}

static void str_free(char **str)
{
	if (!str || !*str)
		return;

	free(*str);

	*str = NULL;
}

static struct iio_context *create_context_from_environment(const char *plugin_desc)
{
	error_t err;
	char *__attribute__((cleanup(str_free)))argz = NULL;
	size_t argz_len;
	char *__attribute__((cleanup(str_free)))plugin = NULL;
	char *entry;

	err = argz_create_sep(plugin_desc, IIO_PLUGIN_DESC_SEPARATOR, &argz,
			&argz_len);
	if (err != 0) {
		errno = err;
		return NULL;
	}

	entry = argz_next(argz, argz_len, NULL);
	if (!entry) {
		ERROR("empty "IIO_BACKEND_PLUGIN_ENV_VAR" environment var\n");
		errno = EINVAL;
		return NULL;
	}

	plugin = strdup(entry);
	if (!plugin)
		return NULL;
	argz_delete(&argz, &argz_len, entry);

	return iio_create_context(plugin, argz, argz_len);
}

int iio_context_init(struct iio_context *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->nb_devices; i++)
		reorder_channels(ctx->devices[i]);

	if (!ctx->xml) {
		ctx->xml = iio_context_create_xml(ctx);
		if (!ctx->xml)
			return -ENOMEM;
	}

	return 0;
}

int iio_context_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	if (ctx->ops->get_version)
		return ctx->ops->get_version(ctx, major, minor, git_tag);

	iio_library_get_version(major, minor, git_tag);
	return 0;
}

int iio_context_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	if (ctx->ops->set_timeout)
		return ctx->ops->set_timeout(ctx, timeout);
	else
		return -ENOSYS;
}

int iio_context_get_timeout(struct iio_context *ctx,
		unsigned int *timeout)
{
	if (ctx->ops->get_timeout)
		return ctx->ops->get_timeout(ctx, timeout);
	else
		return -ENOSYS;
}

struct iio_context * iio_context_clone(const struct iio_context *ctx)
{
	if (ctx->ops->clone) {
		return ctx->ops->clone(ctx);
	} else {
		errno = ENOSYS;
		return NULL;
	}
}

struct iio_context * iio_create_context_from_uri(const char *uri)
{
#ifdef WITH_LOCAL_BACKEND
	if (strcmp(uri, "local:") == 0) /* No address part */
		return iio_create_local_context();
#endif

#ifdef WITH_XML_BACKEND
	if (strncmp(uri, "xml:", sizeof("xml:") - 1) == 0)
		return iio_create_xml_context(uri + sizeof("xml:") - 1);
#endif

#ifdef WITH_NETWORK_BACKEND
	if (strncmp(uri, "ip:", sizeof("ip:") - 1) == 0)
		return iio_create_network_context(uri+3);
#endif

#ifdef WITH_USB_BACKEND
	if (strncmp(uri, "usb:", sizeof("usb:") - 1) == 0)
		return usb_create_context_from_uri(uri);
#endif

#ifdef WITH_SERIAL_BACKEND
	if (strncmp(uri, "serial:", sizeof("serial:") - 1) == 0)
		return serial_create_context_from_uri(uri);
#endif

	errno = ENOSYS;
	return NULL;
}

struct iio_context * iio_create_default_context(void)
{
	/*
	 * the IIO_BACKEND_PLUGIN shall contain a string of the form :
	 *   PLUGIN_NAME[:KEY1=VALUE1[:KEY2=VALUE2[...]]]
	 *   with (KEYX, VALUEX) pairs being the factory properties used to
	 *   tune the factory's constructor's behavior
	 */
	char *plugin_desc = getenv("IIO_BACKEND_PLUGIN");

	DEBUG("IIO_BACKEND_PLUGIN contains \"%s\"\n", plugin_desc);

	if (plugin_desc)
		return create_context_from_environment(plugin_desc);

	char *hostname = getenv("IIOD_REMOTE");

	if (hostname) {
		struct iio_context *ctx;

		ctx = iio_create_context_from_uri(hostname);
		if (ctx)
			return ctx;

#ifdef WITH_NETWORK_BACKEND
		/* If the environment variable is an empty string, we will
		 * discover the server using ZeroConf */
		if (strlen(hostname) == 0)
			hostname = NULL;

		return iio_create_network_context(hostname);
#endif
	}

	return iio_create_local_context();
}

struct iio_context * iio_create_local_context(void)
{
#ifdef WITH_LOCAL_BACKEND
	return local_create_context();
#else
	errno = ENOSYS;
	return NULL;
#endif
}

struct iio_context * iio_create_network_context(const char *hostname)
{
#ifdef WITH_NETWORK_BACKEND
	return network_create_context(hostname);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

struct iio_context * iio_create_xml_context_mem(const char *xml, size_t len)
{
#ifdef WITH_XML_BACKEND
	return xml_create_context_mem(xml, len);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

struct iio_context * iio_create_xml_context(const char *xml_file)
{
#ifdef WITH_XML_BACKEND
	return xml_create_context(xml_file);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

unsigned int iio_context_get_attrs_count(const struct iio_context *ctx)
{
	return ctx->nb_attrs;
}

int iio_context_get_attr(const struct iio_context *ctx, unsigned int index,
		const char **name, const char **value)
{
	if (index >= ctx->nb_attrs)
		return -EINVAL;

	if (name)
		*name = ctx->attrs[index];
	if (value)
		*value = ctx->values[index];
	return 0;
}

const char * iio_context_get_attr_value(
		const struct iio_context *ctx, const char *name)
{
	unsigned int i;

	for (i = 0; i < ctx->nb_attrs; i++) {
		if (!strcmp(name, ctx->attrs[i]))
			return ctx->values[i];
	}

	return NULL;
}

int iio_context_add_attr(struct iio_context *ctx,
		const char *key, const char *value)
{
	char **attrs, **values, *new_key, *new_val;

	attrs = realloc(ctx->attrs,
			(ctx->nb_attrs + 1) * sizeof(*ctx->attrs));
	if (!attrs)
		return -ENOMEM;

	ctx->attrs = attrs;

	values = realloc(ctx->values,
			(ctx->nb_attrs + 1) * sizeof(*ctx->values));
	if (!values)
		return -ENOMEM;

	ctx->values = values;

	new_key = iio_strdup(key);
	if (!new_key)
		return -ENOMEM;

	new_val = iio_strdup(value);
	if (!new_val) {
		free(new_key);
		return -ENOMEM;
	}

	ctx->attrs[ctx->nb_attrs] = new_key;
	ctx->values[ctx->nb_attrs] = new_val;
	ctx->nb_attrs++;
	return 0;
}
