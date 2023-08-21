/*
 * Copyright (c) 2018, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdbool.h>
#include <yaml.h>

#include "device.h"
#include "alpaca.h"
#include "ftdi-gpio.h"
#include "cdb_assist.h"
#include "conmux.h"
#include "console.h"
#include "qcomlt_dbg.h"

#define TOKEN_LENGTH	16384

struct device_parser {
	yaml_parser_t parser;
	yaml_event_t event;
};

static void nextsym(struct device_parser *dp)
{
	if (!yaml_parser_parse(&dp->parser, &dp->event)) {
		fprintf(stderr, "device parser: error %u\n", dp->parser.error);
		exit(1);
	}
}

static int accept(struct device_parser *dp, int type, char *scalar)
{
	if (dp->event.type == type) {
		if (scalar) {
			strncpy(scalar, (char *)dp->event.data.scalar.value, TOKEN_LENGTH - 1);
			scalar[TOKEN_LENGTH - 1] = '\0';
		}

		yaml_event_delete(&dp->event);
		nextsym(dp);
		return true;
	} else {
		return false;
	}
}

static bool expect(struct device_parser *dp, int type, char *scalar)
{
	if (accept(dp, type, scalar)) {
		return true;
	}

	fprintf(stderr, "device parser: expected %d got %u\n", type, dp->event.type);
	exit(1);
}

static void parse_reset_sequence(struct device *dev, char *seq)
{
	char *p, *pp = seq;
	int i = 0;

	dev->custom_reset_sequence = true;

	/* Parse a sequence of "<btn>;time_ms" pairs */
	while(i < MAX_RESET_SEQUENCE) {
		p = strstr(pp, ";");
		if (!p)
			break;
		*p = '\0';

		switch (*pp) {
		case 'B':
			dev->reset_sequence[i].asserted = true;
			__attribute__ ((fallthrough));
		case 'b':
			dev->reset_sequence[i].key = DEVICE_KEY_POWER;
			break;
		case 'R':
			dev->reset_sequence[i].asserted = true;
			__attribute__ ((fallthrough));
		case 'r':
			dev->reset_sequence[i].key = DEVICE_KEY_FASTBOOT;
			break;
		default:
			dev->reset_sequence[i].sleep_ms = strtol(pp, &p, 10);
			if (*p == *pp) {
				fprintf(stderr, "device parser: invalid reset sequence: '%s'\n", seq);
				exit(1);
			}
			i++;
		}

		pp += strlen(pp) + 1;
	}

	if (i == MAX_RESET_SEQUENCE) {
		fprintf(stderr, "device parser: too many reset sequence steps\n");
		exit(1);
	}

	dev->reset_sequence_count = i;

	// for(i = 0; i < dev->reset_sequence_count; i++) {
	// 	fprintf(stderr, "rst %d: %8s %d %d\n", i,
	// 		dev->reset_sequence[i].key ? "power" : "fastboot",
	// 		dev->reset_sequence[i].asserted,
	// 		dev->reset_sequence[i].sleep_ms);
	// }
}

static void parse_board(struct device_parser *dp)
{
	struct device *dev;
	char value[TOKEN_LENGTH];
	char key[TOKEN_LENGTH];

	dev = calloc(1, sizeof(*dev));

	while (accept(dp, YAML_SCALAR_EVENT, key)) {
		expect(dp, YAML_SCALAR_EVENT, value);

		if (!strcmp(key, "board")) {
			dev->board = strdup(value);
		} else if (!strcmp(key, "name")) {
			dev->name = strdup(value);
		} else if (!strcmp(key, "cdba")) {
			dev->control_dev = strdup(value);

			dev->open = cdb_assist_open;
			dev->close = cdb_assist_close;
			dev->power = cdb_assist_power;
			dev->print_status = cdb_assist_print_status;
			dev->usb = cdb_assist_usb;
			dev->key = cdb_assist_key;
		} else if (!strcmp(key, "conmux")) {
			dev->control_dev = strdup(value);

			dev->open = conmux_open;
			dev->power = conmux_power;
			dev->write = conmux_write;
		} else if (!strcmp(key, "alpaca")) {
			dev->control_dev = strdup(value);

			dev->open = alpaca_open;
			dev->power = alpaca_power;
			dev->usb = alpaca_usb;
			dev->key = alpaca_key;
		} else if (!strcmp(key, "ftdi_gpio")) {
			dev->control_dev = strdup(value);

			dev->open = ftdi_gpio_open;
			dev->power = ftdi_gpio_power;
			dev->usb = ftdi_gpio_usb;
			dev->key = ftdi_gpio_key;
		} else if (!strcmp(key, "qcomlt_debug_board")) {
			dev->control_dev = strdup(value);

			dev->open = qcomlt_dbg_open;
			dev->power = qcomlt_dbg_power;
			dev->usb = qcomlt_dbg_usb;
			dev->key = qcomlt_dbg_key;
		} else if (!strcmp(key, "console")) {
			dev->console_dev = strdup(value);
			dev->write = console_write;
			dev->send_break = console_send_break;
		} else if (!strcmp(key, "voltage")) {
			dev->voltage = strtoul(value, NULL, 10);
		} else if (!strcmp(key, "fastboot")) {
			dev->serial = strdup(value);

			if (!dev->boot)
				dev->boot = device_fastboot_boot;
		} else if (!strcmp(key, "fastboot_set_active")) {
			dev->set_active = !strcmp(value, "true");
		} else if (!strcmp(key, "broken_fastboot_boot")) {
			if (!strcmp(value, "true"))
				dev->boot = device_fastboot_flash_reboot;
		} else if (!strcmp(key, "description")) {
			dev->description = strdup(value);
		} else if (!strcmp(key, "fastboot_key_timeout")) {
			dev->fastboot_key_timeout = strtoul(value, NULL, 10);
		} else if (!strcmp(key, "usb_always_on")) {
			dev->usb_always_on = !strcmp(value, "true");
		} else if (!strcmp(key, "reset_sequence")) {
			parse_reset_sequence(dev, value);
		} else {
			fprintf(stderr, "device parser: unknown key \"%s\"\n", key);
			exit(1);
		}
	}

	if (!dev->board || !dev->serial || !(dev->open || dev->console_dev)) {
		fprintf(stderr, "device parser: insufficiently defined device\n");
		exit(1);
	}

	device_add(dev);
}

int device_parser(const char *path)
{
	struct device_parser dp;
	char key[TOKEN_LENGTH];
	FILE *fh;

	fh = fopen(path, "r");
	if (!fh)
		return -1;

	if(!yaml_parser_initialize(&dp.parser)) {
		fprintf(stderr, "device parser: failed to initialize parser\n");
		return -1;
	}

	yaml_parser_set_input_file(&dp.parser, fh);

	nextsym(&dp);

	expect(&dp, YAML_STREAM_START_EVENT, NULL);

	expect(&dp, YAML_DOCUMENT_START_EVENT, NULL);
	expect(&dp, YAML_MAPPING_START_EVENT, NULL);

	if (accept(&dp, YAML_SCALAR_EVENT, key)) {
		expect(&dp, YAML_SEQUENCE_START_EVENT, NULL);

		while (accept(&dp, YAML_MAPPING_START_EVENT, NULL)) {
			parse_board(&dp);
			expect(&dp, YAML_MAPPING_END_EVENT, NULL);
		}

		expect(&dp, YAML_SEQUENCE_END_EVENT, NULL);
	}

	expect(&dp, YAML_MAPPING_END_EVENT, NULL);
	expect(&dp, YAML_DOCUMENT_END_EVENT, NULL);
	expect(&dp, YAML_STREAM_END_EVENT, NULL);

	yaml_event_delete(&dp.event);
	yaml_parser_delete(&dp.parser);

	fclose(fh);

	return 0;
}
