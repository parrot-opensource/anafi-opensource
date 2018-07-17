/* YMM-approved ugliness */
#include <linux/version.h>
/* codecheck_ignore[LINUX_VERSION_CODE] */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
#include "ulogger-4.x.h"
#else
#include "ulogger-3.x.h"
#endif
