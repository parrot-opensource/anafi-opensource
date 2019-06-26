/**
 * Copyright (c) 2017 Parrot Drones
 *
 * @file amba_stepper_regs.h
 * @brief Ambarella Stepper/US driver
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @date 2017-08-10
 */

#ifndef _AMBA_STEPPER_REGS_H__
#define _AMBA_STEPPER_REGS_H__

#include <asm/io.h>      /* writel_relaxed */

#define STEPPER_MOTOR_A_OFFSET  0x000
#define STEPPER_MOTOR_B_OFFSET  0x080
#define STEPPER_MOTOR_C_OFFSET  0x100
#define STEPPER_MOTOR_D_OFFSET  0x180
#define STEPPER_MOTOR_E_OFFSET  0x200

#define STEPPER_CTRL_OFFSET     0x00  /* 0x00(RW): Stepper Control Register */
#define STEPPER_PATTERN_OFFSET  0x04  /* 0x04-0x20(RW): Stepper Pattern Registers */
#define STEPPER_COUNT_OFFSET    0x24  /* 0x24(RW): Stepper Count Register */
#define STEPPER_STATUS_OFFSET   0x28  /* 0x28(RO): Stepper Status Register */

#define write_stepper(offset, value)  writel_relaxed((value), ((ctx.regbase + \
                                      STEPPER_MOTOR_OFFSET + offset)))
#define read_stepper(offset)          readl_relaxed(ctx.regbase +  \
                                      STEPPER_MOTOR_OFFSET + offset)

typedef union _amba_pwm_step_ctrl_reg_u_ {
	u32  data;

	struct {
		u32  clkdivider:       12;  /* [11:0] phase clock = gclk_motor /
		                               (2*(clkdivider + 1)) */
		u32  lastpinstate:     1;   /* [12] 0 = logical low, 1 = logical high */
		u32  uselastpinstate:  1;   /* [13] last pin state is, 0 = same as
		                               last phase, 1 = lastpinstate */
		u32  reserved0:        2;   /* [15:14] reserved */
		u32  patternsize:      6;   /* [21:16] pattern size = (patternsize + 1)
		                               from msb */
		u32  reserved1:        9;   /* [30:22] reserved */
		u32  reset:            1;   /* [31] 1 = reset internal bit pointer of
		                               pattern */
	} bits;
} amba_pwm_step_ctrl_reg_u;

typedef union _amba_pwm_step_count_reg_u_ {
	u32  data;
	struct {
		u32  numphase:    16; /* [15:0] number of phases to be done */
		u32  repeatfirst: 7;  /* [22:16] output the first phase
		                         (4*repeatfirst + 1) times */
		u32  rewind:      1;  /* [23] 1 = rewind */
		u32  repeatlast:  7;  /* [30:24] output the last phase
		                         (4*repeatlast + 1) times */
		u32  reserved:    1;  /* [31] reserved */
	} bits;

} amba_pwm_step_count_reg_u;

#endif
