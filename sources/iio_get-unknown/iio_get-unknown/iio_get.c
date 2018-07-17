/*
 * Testing libiio - Library for interfacing industrial I/O (IIO) devices
 * iio_get.c
 *
 * Original version: iio_readdev.c
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * Modified version: iio_get.c
 * Author: Didier Leymarie <didier.leymarie.ext@parrot.com>
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
 */

#include <iio.h>

#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>

#define MY_NAME "iio_get"

#define SAMPLES_PER_READ 256

enum backend {
	LOCAL,
	NETWORK,
};

struct iio_channel_userdata {
	double		scale;
	double		offset;
	size_t		bytes;
	bool		is_signed;
	bool		is_timestamp;
};

static bool channel_has_attr(struct iio_channel *chn, const char *attr)
{
	unsigned int i, nb = iio_channel_get_attrs_count(chn);
	for (i = 0; i < nb; i++) {
		if (!strcmp(attr, iio_channel_get_attr(chn, i)))
			return true;
	}
	return false;
}

static void iio_channel_set_userdata(struct iio_channel *chn)
{
	char buf[1024];
	struct iio_channel_userdata *data;
	const struct iio_data_format *data_format;

	printf("- Channel: %s\n", iio_channel_get_id(chn));
	data = malloc(sizeof(*data));
	if (data) {
		data->scale = (double)1.0;
		data->offset = (double)0.0;
		data_format = iio_channel_get_data_format(chn);

		data->bytes = data_format->length / 8;
		if (data_format->length % 8)
			data->bytes++;

		data->is_signed = data_format->is_signed;

		data->is_timestamp = !strcmp(iio_channel_get_id(chn),
					     "timestamp");

		if (channel_has_attr(chn, "raw")) {
			if (channel_has_attr(chn, "offset")) {
				iio_channel_attr_read(chn, "offset",
						      buf, sizeof(buf));
				data->offset = atof(buf);
			}
			if (channel_has_attr(chn, "scale")) {
				iio_channel_attr_read(chn, "scale",
						      buf, sizeof(buf));
				data->scale = atof(buf);
			}
		}
		printf(". %s %zu bits\n", data->is_signed ?
		       "Signed" : "Unsigned", data->bytes * 8);
		if (!data->is_timestamp) {
			printf(". Scale  : %18.9lf\n", data->scale);
			printf(". Offset : %18.9lf\n", data->offset);
		} else {
			printf(". Timestamp in nanoseconds\n");
		}

		iio_channel_set_data(chn, data);
	}
}

static void iio_channel_scale(const struct iio_channel *chn,
			      double *value, const void *src)
{
	uint64_t buffer;
	void *dst_raw = &buffer;
	struct iio_channel_userdata *data = iio_channel_get_data(chn);
	double val = 0.0;

	iio_channel_convert(chn, dst_raw, src);

	switch (data->bytes) {
	case 1:
		if (data->is_signed)
			val = ((int8_t *)dst_raw)[0] * 1.0;
		else
			val = ((uint8_t *)dst_raw)[0] * 1.0;
		break;
	case 2:
		if (data->is_signed)
			val = ((int16_t *)dst_raw)[0] * 1.0;
		else
			val = ((uint16_t *)dst_raw)[0] * 1.0;
		break;
	case 4:
		if (data->is_signed)
			val = ((int32_t *)dst_raw)[0] * 1.0;
		else
			val = ((uint32_t *)dst_raw)[0] * 1.0;
		break;
	case 8:
		if (data->is_signed)
			val = ((int64_t *)dst_raw)[0] * 1.0;
		else
			val = ((uint64_t *)dst_raw)[0] * 1.0;
		break;
	default:
		break;
	}

	val += data->offset;
	val *= data->scale;

	*value = val;
}

static void iio_channel_fprintf(const struct iio_channel *chn,
				FILE *out,
				const void *src)
{
	struct iio_channel_userdata *data = iio_channel_get_data(chn);
	double dst;
	int64_t timestamp;

	if (!data->is_timestamp) {
		iio_channel_scale(chn, &dst, src);
		fprintf(out, "%18.9lf ", dst);
	} else {
		timestamp = ((const int64_t *)src)[0];
		fprintf(out, "%10" PRId64".%09" PRId64 " ",
			timestamp / 1000000000,
			timestamp % 1000000000);
	}
}

static const struct option options[] = {
	{"help", no_argument, 0, 'h'},
	{"network", required_argument, 0, 'n'},
	{"trigger", required_argument, 0, 't'},
	{"buffer-size", required_argument, 0, 'b'},
	{"samples", required_argument, 0, 's' },
	{0, 0, 0, 0},
};

static const char *short_options = "hn:t:b:s:";

static const char * const options_descriptions[] = {
	"Show this help and quit.",
	"Use the network backend with the provided hostname.",
	"Use the specified trigger.",
	"Size of the capture buffer. Default is 256.",
	"Number of samples to capture, 0 = infinite. Default is 0."
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-n <hostname>] [-t <trigger>] "
	       "<iio_device> [<channel> ...]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

static struct iio_context *ctx;
struct iio_buffer *buffer;
static const char *trigger_name;
int num_samples;
unsigned int num_channels;

static bool app_running = true;
static int exit_code = EXIT_SUCCESS;

static void quit_all(int sig)
{
	exit_code = sig;
	app_running = false;
}

static void set_handler(int signal_nb, void (*handler)(int))
{
	struct sigaction sig;
	sigaction(signal_nb, NULL, &sig);
	sig.sa_handler = handler;
	sigaction(signal_nb, &sig, NULL);
}

static struct iio_device *get_device(const char *id)
{

	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);
	struct iio_device *device;

	for (i = 0; i < nb_devices; i++) {
		const char *name;
		device = iio_context_get_device(ctx, i);
		name = iio_device_get_name(device);
		if (name && !strcmp(name, id))
			break;
		if (!strcmp(id, iio_device_get_id(device)))
			break;
	}

	if (i < nb_devices)
		return device;

	printf("Device %s not found\n", id);
	return NULL;
}

static ssize_t print_sample(const struct iio_channel *chn,
			   void *buf, size_t len, void *d)
{
	static unsigned ch_counter;
	iio_channel_fprintf(chn, stdout, buf);
	ch_counter++;
	if (ch_counter == num_channels) {
		fprintf(stdout, "\n");
		if (num_samples != 0) {
			num_samples--;
			if (num_samples == 0) {
				quit_all(EXIT_SUCCESS);
				return -1;
			}
		}
		ch_counter = 0;
	}
	return len;
}

int main(int argc, char **argv)
{
	unsigned int i, nb_channels;
	int buffer_size = SAMPLES_PER_READ;
	int c, option_index = 0;
	enum backend backend = LOCAL;
	struct iio_device *dev;
	char *hostname = NULL, *device_name = NULL;

	while ((c = getopt_long(argc, argv, short_options,
				options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'n':
			if (backend != LOCAL) {
				printf("-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = NETWORK;
			hostname = optarg;
			break;
		case 't':
			trigger_name = optarg;
			break;
		case 'b':
			buffer_size = atoi(optarg);
			break;
		case 's':
			num_samples = atoi(optarg);
			break;
		case '?':
			return EXIT_FAILURE;
		default:
			break;
		}
	}

	if (optind < argc) {
		if ((argc - optind) < 1) {
			fprintf(stderr, "Not enough arguments.\n");
			usage();
			fprintf(stderr, "Aborting!\n");
			exit(EXIT_FAILURE);
		}
		device_name = argv[optind+0];
	} else {
		fprintf(stderr, "Not enough arguments.\n");
		usage();
		fprintf(stderr, "Aborting!\n");
		exit(EXIT_FAILURE);
	}

	if (backend == NETWORK)
		ctx = iio_create_network_context(hostname);
	else
		ctx = iio_create_local_context();

	if (!ctx) {
		printf("Unable to create IIO context\n");
		return EXIT_FAILURE;
	}

	set_handler(SIGINT, &quit_all);
	set_handler(SIGSEGV, &quit_all);
	set_handler(SIGTERM, &quit_all);

	dev = get_device(device_name);
	if (!dev) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	printf("- Device %s\n", device_name);

	if (trigger_name) {
		struct iio_device *trigger = get_device(trigger_name);
		if (!trigger) {
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}

		if (!iio_device_is_trigger(trigger)) {
			printf("Specified device is not a trigger\n");
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}

		/* Fixed rate for now */
		iio_device_attr_write_longlong(trigger, "frequency", 100);
		iio_device_set_trigger(dev, trigger);
		printf("- Trigger %s\n", trigger_name);
	}

	nb_channels = iio_device_get_channels_count(dev);

	if ((argc - optind) < 2) {
		/* Enable all channels */
		for (i = 0; i < nb_channels; i++) {
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			if (iio_channel_is_scan_element(ch) &&
			    !iio_channel_is_output(ch)) {
				iio_channel_enable(ch);
				iio_channel_set_userdata(ch);
				num_channels++;
			}
		}
	} else {
		for (i = 0; i < nb_channels; i++) {
			int j;
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			for (j = optind + 1; j < argc; j++) {
				const char *n = iio_channel_get_name(ch);
				if (!strcmp(argv[j], iio_channel_get_id(ch)) ||
				    (n && !strcmp(n, argv[j]))) {
					if (iio_channel_is_scan_element(ch) &&
					    !iio_channel_is_output(ch)) {
						iio_channel_enable(ch);
						iio_channel_set_userdata(ch);
						num_channels++;
					}
				}
			}
		}
	}
	printf("- %d channels\n", num_channels);

	buffer = iio_device_create_buffer(dev, buffer_size, false);
	if (!buffer) {
		printf("Unable to allocate buffer\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	printf("- Sample size: %d bytes\n", 
				(int)iio_device_get_sample_size(dev));

	while (app_running) {
		int ret = iio_buffer_refill(buffer);
		if (ret < 0) {
			printf("Unable to refill buffer: %s\n", strerror(-ret));
			break;
		}
		printf("iio_buffer_refill read %d bytes\n", ret);
		iio_buffer_foreach_sample(buffer, print_sample, NULL);
		fflush(stdout);
	}

	iio_buffer_destroy(buffer);
	iio_context_destroy(ctx);
	return exit_code;
}
