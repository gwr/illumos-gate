

#ifndef _SYS_CPUVAR_H
#define	_SYS_CPUVAR_H

#include <sys/types.h>
#include <sys/thread.h>
#include <sys/sysinfo.h>	/* has cpu_stat_t definition */
#include <sys/disp.h>
#include <sys/processor.h>
#include <sys/kcpc.h>		/* has kcpc_ctx_t definition */
#include <sys/bitmap.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct cpu {
	processorid_t	cpu_id;			/* CPU number */
	processorid_t	cpu_seqid;	/* sequential CPU id (0..ncpus-1) */
} cpu_t;

extern struct cpu	*cpu[];		/* indexed by CPU number */

extern int		boot_max_ncpus;	/* like max_ncpus but for real */
extern int		max_ncpus;	/* max present before ncpus is known */

/* extern struct cpu *curcpup(void); */
#define	CPU		(curcpup())	/* Pointer to current CPU */

extern kmutex_t	cpu_lock;	/* lock protecting CPU data */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_CPUVAR_H */
