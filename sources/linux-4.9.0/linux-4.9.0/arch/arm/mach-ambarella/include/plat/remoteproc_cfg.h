/**
 * History:
 *    2012/09/17 - [Tzu-Jung Lee] created file
 *
 * Copyright (C) 2012-2012, Ambarella, Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of Ambarella, Inc.
 */

#ifndef __PLAT_AMBARELLA_REMOTEPROC_CFG_H
#define __PLAT_AMBARELLA_REMOTEPROC_CFG_H

/*
 * [Copy from the virtio_ring.h]
 *
 * The standard layout for the ring is a continuous chunk of memory which looks
 * like this.  We assume num is a power of 2.
 *
 * struct vring
 * {
 *	// The actual descriptors (16 bytes each)
 *	struct vring_desc desc[num];
 *
 *	// A ring of available descriptor heads with free-running index.
 *	__u16 avail_flags;
 *	__u16 avail_idx;
 *	__u16 available[num];
 *	__u16 used_event_idx;
 *
 *	// Padding to the next align boundary.
 *	char pad[];
 *
 *	// A ring of used descriptor heads with free-running index.
 *	__u16 used_flags;
 *	__u16 used_idx;
 *	struct vring_used_elem used[num];
 *	__u16 avail_event_idx;
 * };
 */

/*
 * Calculation of memory usage for a descriptior rings with n buffers:
 *
 *   (16 * n) + 2 + 2 + (2 * n) + PAD + 2 + 2 + (8 * n) + 2
 *
 *   = (26 * n) + 10 + PAD( < 4K)
 *
 * EX:
 *   A RPMSG bus with 4096 buffers has two descriptor rings with 2048
 *   descriptors each. And each of the them needs:
 *
 *   26 * 2K + 10 + 4K = 56 K + 10 bytes
 */

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK

#include <plat/ambalink_cfg.h>

#endif

#endif /* __PLAT_AMBARELLA_REMOTEPROC_CFG_H */
