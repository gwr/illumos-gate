/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Supply namespaced lifecycle context and the historical physio callback
 * model for the reproducible usbprn-with-USBA OSLL characterization.
 */

#include <sys/types.h>
#include <sys/note.h>

struct buf;
struct modinfo;
struct uio;

#define	MODULE_DECLARATIONS(name)				\
	extern int name##_module_init(void);			\
	extern int name##_module_fini(void);			\
	extern int name##_module_info(struct modinfo *)

MODULE_DECLARATIONS(usba);
MODULE_DECLARATIONS(hubd);
MODULE_DECLARATIONS(ohci);
MODULE_DECLARATIONS(uhci);
MODULE_DECLARATIONS(ehci);
MODULE_DECLARATIONS(usbprn);

int
physio(int (*strategy)(struct buf *), struct buf *bp, dev_t dev, int rw,
    void (*mincnt)(struct buf *), struct uio *uio)
{
	(void) bp;
	(void) dev;
	(void) rw;
	(void) uio;
	(void) strategy((struct buf *)0);
	mincnt((struct buf *)0);
	return (0);
}

int
warlock_dummy(void)
{
	return (0);
}

int
main(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)

	(void) usba_module_init();
	(void) usba_module_fini();
	(void) usba_module_info((struct modinfo *)0);
	(void) hubd_module_init();
	(void) hubd_module_fini();
	(void) hubd_module_info((struct modinfo *)0);
	(void) ohci_module_init();
	(void) ohci_module_fini();
	(void) ohci_module_info((struct modinfo *)0);
	(void) uhci_module_init();
	(void) uhci_module_fini();
	(void) uhci_module_info((struct modinfo *)0);
	(void) ehci_module_init();
	(void) ehci_module_fini();
	(void) ehci_module_info((struct modinfo *)0);
	(void) usbprn_module_init();
	(void) usbprn_module_fini();
	(void) usbprn_module_info((struct modinfo *)0);
	return (0);
}
