/**
 * Copyright (c) 2017 Parrot Drones
 *
 * @file amba_stepper.h
 * @brief Header containing shared data structures with Linux side
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2017-08-10
 */

#ifndef __AMBA_STEPPER_H__
#define __AMBA_STEPPER_H__

	/* (command name, default nb of pulses) */
#define TX_PULSE_COMMANDS(_) \
	_(TX_PULSE_DISABLE,      0) \
	_(TX_PULSE_ENABLE,       0) \
	_(TX_PULSE_SHORT_RANGE,  8) \
	_(TX_PULSE_LONG_RANGE,  16) \
	_(TX_PULSE_PURGE,       32) \
	_(TX_PULSE_SEND,         0) \
	_(TX_PULSE_COMMANDS_NUM, 0) \
	_(TX_PULSE_FORCE_ENUM_SIZE=0xffffffff, 0)

enum amba_parrot_tx_pulse_cmd_e {
#define AS_ENUM(c, n) c,
	TX_PULSE_COMMANDS(AS_ENUM)
#undef AS_ENUM
};

#endif
