/**
 * Copyright (C) 2017 Parrot S.A.
 *     Author: Alexandre Dilly <alexandre.dilly@parrot.com>
 */

#ifndef _PSFS_H_
#define _PSFS_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

/* Some libc doesn't define loff_t */
#ifndef loff_t
typedef long long loff_t;
#endif

/* Shared file system rpmsg channel */
#define PSFS_RPMSG_CHANNEL "psfs"

/* Maximal path name length */
#define PSFS_PATH_MAX_LENGTH 256

/* Address type to handle 32/64-bits pointers */
typedef uint64_t psfs_addr_t;

/*
 * Shared file system commands
 */
enum psfs_cmd {
	/* File functions */

	/* Open file:
	 *  - in:  'struct psfs_params_open'
	 *  - out: 'psfs_addr_t' as file pointer
	 */
	PSFS_CMD_OPEN = 0,
	/* Read file:
	 *  - in: 'struct psfs_params_io'
	 *  - out: 'ssize_t' as read bytes
	 */
	PSFS_CMD_READ,
	/* Write file:
	 *  - in: 'struct psfs_params_io'
	 *  - out: 'ssize_t' as written bytes
	 */
	PSFS_CMD_WRITE,
	/* Seek file:
	 *  - in: 'struct psfs_params_seek'
	 *  - out: 'off_t' as position
	 */
	PSFS_CMD_SEEK,
	/* Close file:
	 *  - in: 'struct psfs_params_close'
	 *  - out: 'void'
	 */
	PSFS_CMD_CLOSE,

	/* File system functions */

	/* Stat a path:
	 *  - in: 'struct psfs_params_stat'
	 *  - out: 'int' as code return
	 */
	PSFS_CMD_STAT,
	/* Remove file or directory:
	 *  - in: 'struct psfs_params_remove'
	 *  - out: 'int' as code return
	 */
	PSFS_CMD_REMOVE,

	/* File system events */
	PSFS_CMD_EVENT,

	/* Root File system is now mounted
	 *  - in: none, command issued from Linux
	 *  - out: 'void'
	 */
	PSFS_CMD_ROOTFS_MOUNTED = PSFS_CMD_EVENT,
	/* A new device is now mounted:
	 *  - in: none, command issued from Linux
	 *  - out: 'char[PSFS_PATH_MAX_LENGTH]' as path mounted
	 */
	PSFS_CMD_FS_MOUNTED,
	/* One of current device mounted has been unmounted:
	 *  - in: none, command issued from Linux
	 *  - out: 'char[PSFS_PATH_MAX_LENGTH]' as path unmounted
	 */
	PSFS_CMD_FS_UNMOUNTED,

	PSFS_CMD_COUNT
};

/*
 * Shared file system rpmsg
 */
struct psfs_rpmsg {
	uint8_t id;
	uint8_t cmd;
	uint16_t len;
	uint64_t reserved;
	union {
		uint64_t params[0];
		uint64_t reply;
		char path[0];
	};
};

/*
 * Command parameters
 */

/* Flags used for PSFS_CMD_OPEN */
enum psfs_open_flags {
	PSFS_O_RDONLY = 0,
	PSFS_O_WRONLY = (1 << 0),
	PSFS_O_RDWR = (1 << 1),
	PSFS_O_APPEND = (1 << 2),
	PSFS_O_CREAT = (1 << 3),
	PSFS_O_EXCL = (1 << 4),
	PSFS_O_TRUNC = (1 << 5),
};

struct psfs_params_open {
	/* Must contain one of enum psfs_open_flags */
	uint32_t	flags;
	/* String must be null terminated */
	char		name[0];
};

struct psfs_params_io {
	psfs_addr_t	filp;
	psfs_addr_t	buf;
	uint64_t	count;
};

/* Flags used for PSFS_CMD_SEEK */
enum psfs_seek_whence {
	PSFS_SEEK_SET = 0,
	PSFS_SEEK_CUR,
	PSFS_SEEK_END,
};

struct psfs_params_seek {
	psfs_addr_t	filp;
	uint64_t	offset;
	/* Must contain one of enum psfs_seek_whence */
	uint64_t	whence;
};

struct psfs_params_close {
	psfs_addr_t	filp;
};

/* Structure used for PSFS_CMD_STAT */
struct psfs_stat {
	uint64_t	dev;
	uint64_t	ino;
	uint32_t	mode;
	uint64_t	nlink;
	uint32_t	uid;
	uint32_t	gid;
	uint64_t	rdev;
	uint64_t	size;
	uint64_t	blksize;
	uint64_t	blocks;
	uint64_t	atim_sec;
	uint64_t	atim_nsec;
	uint64_t	mtim_sec;
	uint64_t	mtim_nsec;
	uint64_t	ctim_sec;
	uint64_t	ctim_nsec;
};

struct psfs_params_stat {
	/* Address of a struct psfs_stat */
	psfs_addr_t	buf;
	/* String must be null terminated */
	char		name[0];
};

struct psfs_params_remove {
	/* String must be null terminated */
	char		name[0];
};

/*
 * Command size
 */
#define PSFS_CMD_SIZE (sizeof(struct psfs_rpmsg) - 1)
#define PSFS_CMD_OPEN_SIZE(len) \
	(PSFS_CMD_SIZE + sizeof(struct psfs_params_open) + len)
#define PSFS_CMD_READ_SIZE (PSFS_CMD_SIZE + sizeof(struct psfs_params_io))
#define PSFS_CMD_WRITE_SIZE (PSFS_CMD_SIZE + sizeof(struct psfs_params_io))
#define PSFS_CMD_SEEK_SIZE (PSFS_CMD_SIZE + sizeof(struct psfs_params_seek))
#define PSFS_CMD_CLOSE_SIZE (PSFS_CMD_SIZE + sizeof(struct psfs_params_close))

#define PSFS_CMD_STAT_SIZE(len) \
	(PSFS_CMD_SIZE + sizeof(struct psfs_params_stat) + len)
#define PSFS_CMD_REMOVE_SIZE(len) \
	(PSFS_CMD_SIZE + sizeof(struct psfs_params_remove) + len)

#define PSFS_CMD_ROOTFS_MOUNTED_SIZE PSFS_CMD_SIZE
#define PSFS_CMD_FS_MOUNTED_SIZE(len) (PSFS_CMD_SIZE + len)
#define PSFS_CMD_FS_UNMOUNTED_SIZE(len) (PSFS_CMD_SIZE + len)

/*
 * Address helpers
 */
static inline void *psfs_to_ptr(psfs_addr_t addr)
{
#if __SIZEOF_POINTER__ == 8
	return (void *) addr;
#else
	return (void *) (uint32_t) addr;
#endif
}

static inline psfs_addr_t psfs_to_addr(void *ptr)
{
#if __SIZEOF_POINTER__ == 8
	return (uint64_t) ptr;
#else
	return (uint64_t) (uint32_t) ptr;
#endif
}

/*
 * Reply helpers
 */
static inline void psfs_set_reply_void(struct psfs_rpmsg *msg)
{
	msg->reply = 0;
}

static inline void psfs_set_reply_ptr(struct psfs_rpmsg *msg, void *ptr)
{
	msg->reply = psfs_to_addr(ptr);
}

static inline psfs_addr_t psfs_reply_to_addr(uint64_t reply)
{
	return reply;
}

static inline void psfs_set_reply_code(struct psfs_rpmsg *msg, int code)
{
	msg->reply = (uint64_t) code;
}

static inline int psfs_reply_to_code(uint64_t reply)
{
	return (int) reply;
}

static inline void psfs_set_reply_ssize(struct psfs_rpmsg *msg, ssize_t size)
{
	msg->reply = (uint64_t) size;
}

static inline ssize_t psfs_reply_to_ssize(uint64_t reply)
{
	return (ssize_t) reply;
}

static inline void psfs_set_reply_loff(struct psfs_rpmsg *msg, loff_t size)
{
	msg->reply = (uint64_t) size;
}

static inline loff_t psfs_reply_to_loff(uint64_t reply)
{
	return (loff_t) reply;
}

#ifdef THREADX_OS
/* Shared file system events user callbacks
 * These functions are called when one of the PSFS event commands are received
 * from Linux:
 *  - psfs_rootfs_mounted_cb is called when '_ROOTFS_MOUNTED' is received and it
 *    corresponds to availability of Root file system under Linux,
 *  - psfs_fs_mounted_cb is called when '_FS_MOUNTED' is received and it occurs
 *    when a new device is mounted under Linux. The mount point path name is
 *    then given through 'path',
 *  - psfs_fs_unmounted_cb is called when '_FS_UNMOUNTED' is received and it
 *    occurs when one of mounted device is unmounted under Linux.
 */
typedef void (*psfs_rootfs_mounted_cb)(void);
typedef void (*psfs_fs_mounted_cb)(char *path);
typedef void (*psfs_fs_unmounted_cb)(char *path);

/* Initialization */
int psfs_early_init(psfs_rootfs_mounted_cb rootfs_mounted_cb,
		    psfs_fs_mounted_cb fs_mounted_cb,
		    psfs_fs_unmounted_cb fs_unmounted_cb);
int psfs_init(void);

/* File */
uint64_t psfs_open(const char *name, int flags);
ssize_t psfs_read(uint64_t fd, void *buf, size_t count);
ssize_t psfs_write(uint64_t fd, void *buf, size_t count);
loff_t psfs_llseek(uint64_t fd, loff_t offset, int whence);
void psfs_close(uint64_t fd);

/* File system */
int psfs_stat(const char *pathname, struct stat *buf);
int psfs_remove(const char *pathname);
#endif

#endif /* _PSFS_H_ */
