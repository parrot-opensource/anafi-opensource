/**
 * Copyright (C) 2017 Parrot S.A.
 *     Author: Alexandre Dilly <alexandre.dilly@parrot.com>
 */

#ifndef _PSI2C_H_
#define _PSI2C_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <string.h>
#endif

/* Parrot Shared I2C bus rpmsg channel */
#define PSI2C_RPMSG_CHANNEL "psi2c"

/* Address type to handle 32/64-bits pointers */
typedef uint64_t psi2c_addr_t;

enum psi2c_cmd {
	PSI2C_CMD_GET_MSG_BUFFER = 0,
	PSI2C_CMD_TRANSFER,

	PSI2C_CMD_COUNT
};

enum psi2c_state {
	PSI2C_STATE_NONE = 0,
	PSI2C_STATE_SUCCESS,
	PSI2C_STATE_ERROR,

	PSI2C_STATE_COUNT
};

enum psi2c_channel {
	PSI2C_CHANNEL_0 = 0,
	PSI2C_CHANNEL_1,
	PSI2C_CHANNEL_2,
	PSI2C_CHANNEL_3,

	PSI2C_CHANNEL_COUNT
};

struct psi2c_msg_buffer {
	psi2c_addr_t address;
	uint32_t size;
};

struct psi2c_transfer {
	uint16_t slave_address;
	uint16_t tx_size;
	uint16_t rx_size;
};

struct psi2c_rpmsg {
	uint8_t cmd;
	uint8_t channel;
	union {
		struct psi2c_msg_buffer msg_buffer;
		struct psi2c_transfer xfer;
		uint8_t state;
	};
};

/*
 * Address helpers
 */
static inline void *psi2c_to_ptr(psi2c_addr_t addr)
{
#if __SIZEOF_POINTER__ == 8
	return (void *) addr;
#else
	return (void *) (uint32_t) addr;
#endif
}

static inline psi2c_addr_t psi2c_to_addr(void *ptr)
{
#if __SIZEOF_POINTER__ == 8
	return (uint64_t) ptr;
#else
	return (uint64_t) (uint32_t) ptr;
#endif
}

/*
 * Transfer helpers
 */
static inline int psi2c_check_size(unsigned long msg_buffer_size,
				    uint16_t tx_size, uint16_t rx_size)
{
	if (tx_size && rx_size)
		return msg_buffer_size < (tx_size * 2) + rx_size + 4;
	return msg_buffer_size < tx_size + rx_size;
}

static inline void psi2c_set_tx(unsigned char *msg_buffer, unsigned char *tx,
			        uint16_t tx_size, uint16_t rx_size)
{
	uint16_t *buf;

	if (!tx_size)
		return;

	if (rx_size) {
		/* For read after write, we need to store data in 16-bits for
		 * TX part.
		 */
		buf = (uint16_t *) (msg_buffer + 2);
		while (tx_size--)
			*buf++ = *tx++;
	} else
		memcpy(msg_buffer, tx, tx_size);
}

static inline void psi2c_get_rx(unsigned char *msg_buffer, unsigned char *rx,
				uint16_t tx_size, uint16_t rx_size)
{
	if (!rx_size)
		return;

	if (tx_size)
		memcpy(rx, msg_buffer + (tx_size * 2) + 4, rx_size);
	else
		memcpy(rx, msg_buffer, rx_size);
}

#ifdef THREADX_OS
/* Initialization */
int psi2c_early_init(void);
int psi2c_init(void);
#endif

#endif /* _PSI2C_H_ */
