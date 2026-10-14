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
 * Model common kernel entry points and callbacks for locklint analysis.
 *
 * Per-module command files bind the generic operation-vector members to
 * concrete driver functions.  This model covers current leaf-driver dev_ops
 * and cb_ops entries plus the synchronous shape of physio().  Caller-specific
 * binding environments propagate exact function-valued arguments through the
 * physio formals.
 */

#include <sys/types.h>
#include <sys/devops.h>
#include <sys/ddi_intr.h>
#include <sys/epm.h>
#include <sys/modctl.h>
#include <sys/note.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/sunpm.h>
#include <sys/systm.h>

struct dev_ops *locklint_dev_ops;
struct cb_ops *locklint_cb_ops;
struct bus_ops *locklint_bus_ops;
static kmutex_t locklint_delay_mutex;
static kcondvar_t locklint_delay_cv;

/*
 * Give per-module policy type-correct targets for operation-vector members
 * which the selected driver leaves NULL or assigns a kernel no-op routine.
 */
int
locklint_no_dev(dev_info_t *dip)
{
	return (0);
}

int
locklint_no_reset(dev_info_t *dip, ddi_reset_cmd_t cmd)
{
	return (0);
}

int
locklint_no_power(dev_info_t *dip, int component, int level)
{
	return (0);
}

int
locklint_no_strategy(struct buf *bp)
{
	return (0);
}

int
locklint_no_print(dev_t dev, char *str)
{
	return (0);
}

int
locklint_no_dump(dev_t dev, caddr_t addr, daddr_t blkno, int nblk)
{
	return (0);
}

int
locklint_no_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
    size_t *maplen, uint_t model)
{
	return (0);
}

int
locklint_no_mmap(dev_t dev, off_t off, int prot)
{
	return (0);
}

int
locklint_no_segmap(dev_t dev, off_t off, struct as *asp, caddr_t *addrp,
    off_t len, unsigned int prot, unsigned int maxprot, unsigned int flags,
    cred_t *credp)
{
	return (0);
}

int
locklint_no_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
    int mod_flags, char *name, caddr_t valuep, int *length)
{
	return (0);
}

int
locklint_no_aio(dev_t dev, struct aio_req *aio, cred_t *credp)
{
	return (0);
}

int
locklint_no_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
    off_t offset, off_t len, caddr_t *vaddrp)
{
	return (0);
}

int
locklint_no_bus_map_fault(dev_info_t *dip, dev_info_t *rdip, struct hat *hat,
    struct seg *seg, caddr_t addr, struct devpage *dp, pfn_t pfn, uint_t prot,
    uint_t lock)
{
	return (0);
}

int
locklint_no_bus_dma_allochdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_attr_t *attr, int (*waitfp)(caddr_t), caddr_t arg,
    ddi_dma_handle_t *handlep)
{
	return (0);
}

int
locklint_no_bus_dma_handle(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	return (0);
}

int
locklint_no_bus_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	return (0);
}

int
locklint_no_bus_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, size_t len, uint_t cache_flags)
{
	return (0);
}

int
locklint_no_bus_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp, size_t *lenp,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	return (0);
}

int
locklint_no_bus_dma_ctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request, off_t *offp,
    size_t *lenp, caddr_t *objp, uint_t flags)
{
	return (0);
}

int
locklint_no_bus_ctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	return (0);
}

int
locklint_no_bus_prop(dev_t dev, dev_info_t *dip, dev_info_t *child_dip,
    ddi_prop_op_t prop_op, int mod_flags, char *name, caddr_t valuep,
    int *length)
{
	return (0);
}

int
locklint_no_bus_get_event(dev_info_t *dip, dev_info_t *rdip,
    char *eventname, ddi_eventcookie_t *cookiep)
{
	return (0);
}

int
locklint_no_bus_add_event(dev_info_t *dip, dev_info_t *rdip,
    ddi_eventcookie_t eventid, void (*handler)(dev_info_t *,
    ddi_eventcookie_t, void *, void *), void *arg, ddi_callback_id_t *id)
{
	return (0);
}

int
locklint_no_bus_remove_event(dev_info_t *dip, ddi_callback_id_t id)
{
	return (0);
}

int
locklint_no_bus_post_event(dev_info_t *dip, dev_info_t *rdip,
    ddi_eventcookie_t event, void *impl_data)
{
	return (0);
}

int
locklint_no_bus_config(dev_info_t *parent, uint_t flags,
    ddi_bus_config_op_t op, void *arg, dev_info_t **childp)
{
	return (0);
}

int
locklint_no_bus_unconfig(dev_info_t *parent, uint_t flags,
    ddi_bus_config_op_t op, void *arg)
{
	return (0);
}

int
locklint_no_bus_fm_init(dev_info_t *dip, dev_info_t *tdip, int cap,
    ddi_iblock_cookie_t *ibc)
{
	return (0);
}

void
locklint_no_bus_fm_fini(dev_info_t *dip, dev_info_t *tdip)
{
}

void
locklint_no_bus_fm_access(dev_info_t *dip, ddi_acc_handle_t handle)
{
}

int
locklint_no_bus_power(dev_info_t *dip, void *impl_arg,
    pm_bus_power_op_t op, void *arg, void *result)
{
	return (0);
}

int
locklint_no_bus_intr_op(dev_info_t *dip, dev_info_t *rdip,
    ddi_intr_op_t op, ddi_intr_handle_impl_t *hdlp, void *result)
{
	return (0);
}

int
locklint_no_bus_hp(dev_info_t *dip, char *cn_name, ddi_hp_op_t op,
    void *arg, void *result)
{
	return (0);
}

/*
 * Represent module removal invoking the driver's detach entry before the
 * module can be unloaded.
 */
int
mod_remove(struct modlinkage *linkage)
{
	return ((*locklint_dev_ops->devo_detach)(NULL, DDI_DETACH));
}

/*
 * Represent leaf-driver entry points.  Identify, probe, and attach run in the
 * initial single-threaded region.  Ordinary device and character operations
 * can run after the driver has become visible to competing threads.  Quiesce
 * runs after competing driver execution has stopped.
 */
void
locklint_ddi_dki_model(void)
{
	dev_t dev = 0;

	(void) (*locklint_dev_ops->devo_identify)(NULL);
	(void) (*locklint_dev_ops->devo_probe)(NULL);
	(void) (*locklint_dev_ops->devo_attach)(NULL, DDI_ATTACH);

	_NOTE(COMPETING_THREADS_NOW)

	(void) (*locklint_dev_ops->devo_getinfo)(NULL,
	    DDI_INFO_DEVT2INSTANCE, NULL, NULL);
	(void) (*locklint_dev_ops->devo_reset)(NULL, DDI_RESET_FORCE);
	(void) (*locklint_dev_ops->devo_power)(NULL, 0, 0);

	(void) (*locklint_cb_ops->cb_open)(&dev, 0, 0, NULL);
	(void) (*locklint_cb_ops->cb_close)(dev, 0, 0, NULL);
	(void) (*locklint_cb_ops->cb_strategy)(NULL);
	(void) (*locklint_cb_ops->cb_print)(dev, NULL);
	(void) (*locklint_cb_ops->cb_dump)(dev, NULL, 0, 0);
	(void) (*locklint_cb_ops->cb_read)(dev, NULL, NULL);
	(void) (*locklint_cb_ops->cb_write)(dev, NULL, NULL);
	(void) (*locklint_cb_ops->cb_ioctl)(dev, 0, 0, 0, NULL, NULL);
	(void) (*locklint_cb_ops->cb_devmap)(dev, NULL, 0, 0, NULL, 0);
	(void) (*locklint_cb_ops->cb_mmap)(dev, 0, 0);
	(void) (*locklint_cb_ops->cb_segmap)(dev, 0, NULL, NULL, 0, 0, 0,
	    0, NULL);
	(void) (*locklint_cb_ops->cb_chpoll)(dev, 0, 0, NULL, NULL);
	(void) (*locklint_cb_ops->cb_prop_op)(dev, NULL, PROP_LEN, 0, NULL,
	    NULL, NULL);
	(void) (*locklint_cb_ops->cb_aread)(dev, NULL, NULL);
	(void) (*locklint_cb_ops->cb_awrite)(dev, NULL, NULL);

	_NOTE(NO_COMPETING_THREADS_NOW)

	(void) (*locklint_dev_ops->devo_quiesce)(NULL);
}

/*
 * Represent current revision-10 nexus entry points after the nexus is visible
 * to competing threads.  Explicitly obsolete interrupt and DMA-map members
 * are not called.
 */
void
locklint_ddi_dki_bus_model(void)
{
	_NOTE(COMPETING_THREADS_NOW)

	(void) (*locklint_bus_ops->bus_map)(NULL, NULL, NULL, 0, 0, NULL);
	(void) (*locklint_bus_ops->bus_map_fault)(NULL, NULL, NULL, NULL,
	    NULL, NULL, 0, 0, 0);
	(void) (*locklint_bus_ops->bus_dma_allochdl)(NULL, NULL, NULL, NULL,
	    NULL, NULL);
	(void) (*locklint_bus_ops->bus_dma_freehdl)(NULL, NULL, NULL);
	(void) (*locklint_bus_ops->bus_dma_bindhdl)(NULL, NULL, NULL, NULL,
	    NULL, NULL);
	(void) (*locklint_bus_ops->bus_dma_unbindhdl)(NULL, NULL, NULL);
	(void) (*locklint_bus_ops->bus_dma_flush)(NULL, NULL, NULL, 0, 0, 0);
	(void) (*locklint_bus_ops->bus_dma_win)(NULL, NULL, NULL, 0, NULL,
	    NULL, NULL, NULL);
	(void) (*locklint_bus_ops->bus_dma_ctl)(NULL, NULL, NULL, 0, NULL,
	    NULL, NULL, 0);
	(void) (*locklint_bus_ops->bus_ctl)(NULL, NULL, 0, NULL, NULL);
	(void) (*locklint_bus_ops->bus_prop_op)(0, NULL, NULL, PROP_LEN, 0,
	    NULL, NULL, NULL);
	(void) (*locklint_bus_ops->bus_get_eventcookie)(NULL, NULL, NULL,
	    NULL);
	(void) (*locklint_bus_ops->bus_add_eventcall)(NULL, NULL, NULL, NULL,
	    NULL, NULL);
	(void) (*locklint_bus_ops->bus_remove_eventcall)(NULL, NULL);
	(void) (*locklint_bus_ops->bus_post_event)(NULL, NULL, NULL, NULL);
	(void) (*locklint_bus_ops->bus_config)(NULL, 0, 0, NULL, NULL);
	(void) (*locklint_bus_ops->bus_unconfig)(NULL, 0, 0, NULL);
	(void) (*locklint_bus_ops->bus_fm_init)(NULL, NULL, 0, NULL);
	(*locklint_bus_ops->bus_fm_fini)(NULL, NULL);
	(*locklint_bus_ops->bus_fm_access_enter)(NULL, NULL);
	(*locklint_bus_ops->bus_fm_access_exit)(NULL, NULL);
	(void) (*locklint_bus_ops->bus_power)(NULL, NULL, 0, NULL, NULL);
	(void) (*locklint_bus_ops->bus_intr_op)(NULL, NULL, 0, NULL, NULL);
	(void) (*locklint_bus_ops->bus_hp_op)(NULL, NULL, 0, NULL, NULL);

	_NOTE(NO_COMPETING_THREADS_NOW)
}

/*
 * Represent physio's synchronous callback order.  Native locklint binds exact
 * function-valued actuals to these formals in each caller context.
 */
int
physio(int (*strategy)(struct buf *), struct buf *bp, dev_t dev, int rw,
    void (*mincnt)(struct buf *), struct uio *uio)
{
	mincnt(bp);
	return (strategy(bp));
}

/*
 * Represent direct current forwarding from public DDI interfaces to nexus
 * bus operations.  The real framework derives nexus device pointers and DMA
 * ownership from opaque handles; those identities are not needed to preserve
 * the call relationship.
 */
int
ddi_dma_alloc_handle(dev_info_t *dip, const ddi_dma_attr_t *attr,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	return ((*locklint_bus_ops->bus_dma_allochdl)(dip, dip,
	    (ddi_dma_attr_t *)attr, waitfp, arg, handlep));
}

int
ddi_dma_mctl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    enum ddi_dma_ctlops request, off_t *offp, size_t *lenp, caddr_t *objp,
    uint_t flags)
{
	return ((*locklint_bus_ops->bus_dma_ctl)(dip, rdip, handle, request,
	    offp, lenp, objp, flags));
}

int
ddi_dma_sync(ddi_dma_handle_t handle, off_t offset, size_t len, uint_t flags)
{
	return ((*locklint_bus_ops->bus_dma_flush)(NULL, NULL, handle, offset,
	    len, flags));
}

int
ddi_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	return ((*locklint_bus_ops->bus_prop_op)(dev, dip, dip, prop_op,
	    mod_flags, name, valuep, lengthp));
}

int
ddi_get_eventcookie(dev_info_t *dip, char *name,
    ddi_eventcookie_t *event_cookiep)
{
	return ((*locklint_bus_ops->bus_get_eventcookie)(dip, dip, name,
	    event_cookiep));
}

int
ddi_add_event_handler(dev_info_t *dip, ddi_eventcookie_t event,
    void (*handler)(dev_info_t *, ddi_eventcookie_t, void *, void *),
    void *arg, ddi_callback_id_t *id)
{
	return ((*locklint_bus_ops->bus_add_eventcall)(dip, dip, event,
	    handler, arg, id));
}

int
ddi_remove_event_handler(ddi_callback_id_t id)
{
	return ((*locklint_bus_ops->bus_remove_eventcall)(NULL, id));
}

int
pm_set_power(dev_info_t *dip, int component, int level, int direction,
    pm_canblock_t canblock, int scan, int *retp)
{
	return ((*locklint_dev_ops->devo_power)(dip, component, level));
}

int
pm_raise_power(dev_info_t *dip, int component, int level)
{
	return ((*locklint_dev_ops->devo_power)(dip, component, level));
}

int
pm_lower_power(dev_info_t *dip, int component, int level)
{
	return ((*locklint_dev_ops->devo_power)(dip, component, level));
}

int
ndi_devi_online(dev_info_t *dip, uint_t flags)
{
	return ((*locklint_bus_ops->bus_config)(dip, flags, BUS_CONFIG_ALL,
	    NULL, NULL));
}

int
ndi_devi_offline(dev_info_t *dip, uint_t flags)
{
	return ((*locklint_bus_ops->bus_unconfig)(dip, flags,
	    BUS_UNCONFIG_ALL, NULL));
}

/*
 * Represent asynchronous callback registration without making a synchronous
 * call through the callback formal.  Exact function-pointer escapes become
 * independent competing roots with no registration-time locks.
 */
timeout_id_t
timeout(void (*func)(void *), void *arg, clock_t ticks)
{
	return (0);
}

int
ddi_intr_add_handler(ddi_intr_handle_t handle, ddi_intr_handler_t handler,
    void *arg1, void *arg2)
{
	return (DDI_SUCCESS);
}

int
ddi_intr_add_softint(dev_info_t *dip, ddi_softint_handle_t *handlep,
    int soft_pri, ddi_intr_handler_t handler, void *arg1)
{
	return (DDI_SUCCESS);
}

int
ddi_add_intr(dev_info_t *dip, uint_t inumber,
    ddi_iblock_cookie_t *iblock_cookiep,
    ddi_idevice_cookie_t *idevice_cookiep,
    uint_t (*handler)(caddr_t), caddr_t arg)
{
	return (DDI_SUCCESS);
}

int
ddi_add_softintr(dev_info_t *dip, int preference, ddi_softintr_t *idp,
    ddi_iblock_cookie_t *iblock_cookiep,
    ddi_idevice_cookie_t *idevice_cookiep,
    uint_t (*handler)(caddr_t), caddr_t arg)
{
	return (DDI_SUCCESS);
}

void
ddi_set_callback(int (*func)(caddr_t), caddr_t arg, uintptr_t *listid)
{
}

/*
 * Represent delay's current condition wait so locks held by the caller remain
 * visible across the blocking point.
 */
void
delay(clock_t ticks)
{
	mutex_enter(&locklint_delay_mutex);
	cv_wait(&locklint_delay_cv, &locklint_delay_mutex);
	mutex_exit(&locklint_delay_mutex);
}
