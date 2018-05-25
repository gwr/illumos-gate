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
 * Copyright 2018 Nexenta Systems, Inc.
 */

/*
 * This file contains code necessary to send SCSI commands to HBA.
 */
#include <smartpqi.h>

/*
 * []------------------------------------------------------------------[]
 * | Forward declarations for support/utility functions			|
 * []------------------------------------------------------------------[]
 */
static void aio_io_complete(pqi_io_request_t *io, void *context);
static void raid_io_complete(pqi_io_request_t *io, void *context);
static void build_aio_sg_list(pqi_state_t s,
	pqi_aio_path_request_t *rqst, pqi_cmd_t cmd, pqi_io_request_t *);
static void build_raid_sg_list(pqi_state_t s,
	pqi_raid_path_request_t *rqst, pqi_cmd_t cmd, pqi_io_request_t *);
static pqi_io_request_t *setup_aio_request(pqi_state_t s, pqi_cmd_t cmd);
static pqi_io_request_t *setup_raid_request(pqi_state_t s, pqi_cmd_t cmd);
static uint32_t read_heartbeat_counter(pqi_state_t s);
static void take_ctlr_offline(pqi_state_t s);
static uint32_t free_elem_count(pqi_index_t pi, pqi_index_t ci,
	uint32_t per_iq);
static void ack_event(pqi_state_t s, pqi_event_t e);
static boolean_t is_aio_enabled(pqi_device_t d);

#define	DIV_UP(n, d) ((n + (d - 1)) / d)

/*
 * []------------------------------------------------------------------[]
 * | Main entry points in file.						|
 * []------------------------------------------------------------------[]
 */

/*
 * pqi_watchdog -- interrupt count and/or heartbeat must increase over time.
 */
void
pqi_watchdog(void *v)
{
	pqi_state_t	s = v;
	uint32_t	hb;

	if (pqi_is_offline(s))
		return;

	hb = read_heartbeat_counter(s);
	if ((s->s_last_intr_count == s->s_intr_count) &&
	    (s->s_last_heartbeat_count == hb)) {
		dev_err(s->s_dip, CE_NOTE, "No heartbeat");
		pqi_show_dev_state(s);
		take_ctlr_offline(s);
	} else {
		s->s_last_intr_count = s->s_intr_count;
		s->s_last_heartbeat_count = hb;
		s->s_watchdog = timeout(pqi_watchdog, s,
		    drv_usectohz(WATCHDOG));
	}
}

/*
 * pqi_start_io -- queues command to HBA.
 *
 * This method can be called either from the upper layer with a non-zero
 * io argument or called during an interrupt to load the outgoing queue
 * with more commands.
 */
void
pqi_start_io(pqi_state_t s, pqi_queue_group_t *qg, pqi_path_t path,
    pqi_io_request_t *io)
{
	pqi_iu_header_t	*rqst;
	size_t		iu_len,
			copy_to_end;
	pqi_index_t	iq_pi,
			iq_ci;
	uint32_t	elem_needed,
			elem_to_end;
	caddr_t		next_elem;
	int		sending		= 0;

	mutex_enter(&qg->submit_lock[path]);
	if (io != NULL)
		list_insert_tail(&qg->request_list[path], io);


	iq_pi = qg->iq_pi_copy[path];
	while ((io = list_head(&qg->request_list[path])) != NULL) {

		/* ---- Primary cause for !active is controller failure ---- */
		if (qg->qg_active == B_FALSE && io->io_cmd) {
			pqi_fail_cmd(io->io_cmd);
			list_remove(&qg->request_list[path], io);
			continue;
		}

		rqst = io->io_iu;
		iu_len = rqst->iu_length + PQI_REQUEST_HEADER_LENGTH;
		elem_needed = DIV_UP(iu_len, PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);
		(void) ddi_dma_sync(s->s_queue_dma->handle,
		    (uintptr_t)qg->iq_ci[path] -
		    (uintptr_t)s->s_queue_dma->alloc_memory, sizeof (iq_ci),
		    DDI_DMA_SYNC_FORCPU);
		iq_ci = *qg->iq_ci[path];

		if (elem_needed > free_elem_count(iq_pi, iq_ci,
		    s->s_num_elements_per_iq))
			break;

		io->io_pi = iq_pi;
		rqst->iu_id = qg->oq_id;
		next_elem = qg->iq_element_array[path] +
		    (iq_pi * PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);
		elem_to_end = s->s_num_elements_per_iq - iq_pi;
		if (elem_needed <= elem_to_end) {
			(void) memcpy(next_elem, rqst, iu_len);
			(void) ddi_dma_sync(s->s_queue_dma->handle,
			    (uintptr_t)next_elem -
			    (uintptr_t)s->s_queue_dma->alloc_memory, iu_len,
			    DDI_DMA_SYNC_FORDEV);
		} else {
			copy_to_end = elem_to_end *
			    PQI_OPERATIONAL_IQ_ELEMENT_LENGTH;
			(void) memcpy(next_elem, rqst, copy_to_end);
			(void) ddi_dma_sync(s->s_queue_dma->handle,
			    (uintptr_t)next_elem -
			    (uintptr_t)s->s_queue_dma->alloc_memory,
			    copy_to_end, DDI_DMA_SYNC_FORDEV);
			(void) memcpy(qg->iq_element_array[path],
			    (caddr_t)rqst + copy_to_end,
			    iu_len - copy_to_end);
			(void) ddi_dma_sync(s->s_queue_dma->handle,
			    0, iu_len - copy_to_end, DDI_DMA_SYNC_FORDEV);
		}
		sending += elem_needed;
		if (io->io_cmd != NULL)
			pqi_cmd_sm(io->io_cmd, PQI_CMD_STARTED);
		else if ((rqst->iu_type == PQI_REQUEST_IU_RAID_PATH_IO) &&
		    (s->s_debug_level & (DBG_LVL_CDB | DBG_LVL_RQST)))
			pqi_dump_io(io);

		iq_pi = (iq_pi + elem_needed) % s->s_num_elements_per_iq;
		list_remove(&qg->request_list[path], io);
	}

	qg->submit_count += sending;
	if (iq_pi != qg->iq_pi_copy[path]) {
		qg->iq_pi_copy[path] = iq_pi;
		ddi_put32(s->s_datap, qg->iq_pi[path], iq_pi);
	} else {
		ASSERT0(sending);
	}
	mutex_exit(&qg->submit_lock[path]);
}

int
pqi_transport_command(pqi_state_t s, pqi_cmd_t cmd)
{
	pqi_device_t		devp = cmd->pc_device;
	int			path;
	pqi_io_request_t	*io;

	if (is_aio_enabled(devp) == B_TRUE) {
		path = AIO_PATH;
		io = setup_aio_request(s, cmd);
	} else {
		path = RAID_PATH;
		io = setup_raid_request(s, cmd);
	}

	if (io == NULL)
		return (TRAN_BUSY);

	cmd->pc_io_rqst = io;

	pqi_start_io(s, &s->s_queue_groups[PQI_DEFAULT_QUEUE_GROUP],
	    path, io);

	return (TRAN_ACCEPT);
}

void
pqi_event_worker(void *v)
{
	pqi_state_t	s		= v;
	int		i;
	pqi_event_t	e;
	boolean_t	non_heartbeat	= B_FALSE;

	if (pqi_is_offline(s))
		return;

	e = s->s_events;
	for (i = 0; i < PQI_NUM_SUPPORTED_EVENTS; i++) {
		if (e->ev_pending == B_TRUE) {
			e->ev_pending = B_FALSE;
			ack_event(s, e);
			if (pqi_map_event(PQI_EVENT_TYPE_HEARTBEAT) != i)
				non_heartbeat = B_TRUE;
		}
		e++;
	}
	if (non_heartbeat == B_TRUE) {
		pqi_rescan_devices(s);
		(void) pqi_config_all(s->s_dip, s);
	}
}

void
pqi_fail_cmd(pqi_cmd_t cmd)
{
	struct scsi_pkt		*pkt	= CMD2PKT(cmd);

	pkt->pkt_reason = CMD_TRAN_ERR;
	pkt->pkt_statistics |= STAT_DEV_RESET;
	*pkt->pkt_scbp = STATUS_CHECK;
	pqi_cmd_sm(cmd, PQI_CMD_FATAL);
	if (cmd->pc_poll)
		sema_v(cmd->pc_poll);
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0 &&
	    (pkt->pkt_comp != NULL))
		(*pkt->pkt_comp)(pkt);
}

void
pqi_fail_drive_cmds(pqi_device_t devp)
{
	pqi_cmd_t	cmd;

	mutex_enter(&devp->pd_mutex);
	while ((cmd = list_head(&devp->pd_cmd_list)) != NULL) {

		/*
		 * pqi_fail_cmd calls pqi_cmd_sm which will remove
		 * the command from the device active command list
		 * and needs to grab pd_mutex to do so which is why
		 * the lock is dropped and then reaquired.
		 */
		mutex_exit(&devp->pd_mutex);
		pqi_fail_cmd(cmd);
		mutex_enter(&devp->pd_mutex);
	}
	mutex_exit(&devp->pd_mutex);
}

/*
 * []------------------------------------------------------------------[]
 * | Support/utility functions for main entry points			|
 * []------------------------------------------------------------------[]
 */
static void
send_event_ack(pqi_state_t s, pqi_event_acknowledge_request_t *rqst)
{
	pqi_queue_group_t	*qg;
	caddr_t			next_element;
	pqi_index_t		iq_ci,
	iq_pi;

	qg = &s->s_queue_groups[PQI_DEFAULT_QUEUE_GROUP];
	rqst->header.iu_id = qg->oq_id;

	for (;;) {
		mutex_enter(&qg->submit_lock[RAID_PATH]);
		iq_pi = qg->iq_pi_copy[RAID_PATH];
		iq_ci = ddi_get32(s->s_queue_dma->acc, qg->iq_ci[RAID_PATH]);

		if (free_elem_count(iq_pi, iq_ci, s->s_num_elements_per_iq))
			break;

		mutex_exit(&qg->submit_lock[RAID_PATH]);
		if (pqi_is_offline(s))
			return;
	}
	next_element = qg->iq_element_array[RAID_PATH] +
	    (iq_pi * PQI_OPERATIONAL_IQ_ELEMENT_LENGTH);

	(void) memcpy(next_element, rqst, sizeof (*rqst));
	(void) ddi_dma_sync(s->s_queue_dma->handle, 0, 0, DDI_DMA_SYNC_FORDEV);

	iq_pi = (iq_pi + 1) % s->s_num_elements_per_iq;
	qg->iq_pi_copy[RAID_PATH] = iq_pi;

	ddi_put32(s->s_datap, qg->iq_pi[RAID_PATH], iq_pi);

	mutex_exit(&qg->submit_lock[RAID_PATH]);
}

static void
ack_event(pqi_state_t s, pqi_event_t e)
{
	pqi_event_acknowledge_request_t	rqst;

	(void) memset(&rqst, 0, sizeof (rqst));
	rqst.header.iu_type = PQI_REQUEST_IU_ACKNOWLEDGE_VENDOR_EVENT;
	rqst.header.iu_length = sizeof (rqst) - PQI_REQUEST_HEADER_LENGTH;
	rqst.event_type = e->ev_type;
	rqst.event_id = e->ev_id;
	rqst.additional_event_id = e->ev_additional;

	send_event_ack(s, &rqst);
}

static pqi_io_request_t *
setup_aio_request(pqi_state_t s, pqi_cmd_t cmd)
{
	pqi_io_request_t	*io;
	pqi_aio_path_request_t	*rqst;
	pqi_device_t		devp = cmd->pc_device;

	/* ---- Most likely received a signal during a cv_wait ---- */
	if ((io = pqi_alloc_io(s)) == NULL)
		return (NULL);

	io->io_cb = aio_io_complete;
	io->io_cmd = cmd;
	io->io_raid_bypass = 0;

	rqst = io->io_iu;
	(void) memset(rqst, 0, sizeof (*rqst));

	rqst->header.iu_type = PQI_REQUEST_IU_AIO_PATH_IO;
	rqst->nexus_id = devp->pd_aio_handle;
	rqst->buffer_length = cmd->pc_dma_count;
	rqst->task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	rqst->request_id = io->io_index;
	rqst->error_index = rqst->request_id;
	rqst->cdb_length = cmd->pc_cmdlen;
	(void) memcpy(rqst->cdb, cmd->pc_cdb, cmd->pc_cmdlen);
	(void) memcpy(rqst->lun_number, devp->pd_scsi3addr,
	    sizeof (rqst->lun_number));

	if (cmd->pc_flags & PQI_FLAG_DMA_VALID) {
		if (cmd->pc_flags & PQI_FLAG_IO_READ)
			rqst->data_direction = SOP_READ_FLAG;
		else
			rqst->data_direction = SOP_WRITE_FLAG;
	} else {
		rqst->data_direction = SOP_NO_DIRECTION_FLAG;
	}

	build_aio_sg_list(s, rqst, cmd, io);
	return (io);
}

static pqi_io_request_t *
setup_raid_request(pqi_state_t s, pqi_cmd_t cmd)
{
	pqi_io_request_t	*io;
	pqi_raid_path_request_t	*rqst;
	pqi_device_t		devp = cmd->pc_device;

	/* ---- Most likely received a signal during a cv_wait ---- */
	if ((io = pqi_alloc_io(s)) == NULL)
		return (NULL);

	io->io_cb = raid_io_complete;
	io->io_cmd = cmd;
	io->io_raid_bypass = 0;

	rqst = io->io_iu;
	(void) memset(rqst, 0, sizeof (*rqst));
	rqst->header.iu_type = PQI_REQUEST_IU_RAID_PATH_IO;
	rqst->rp_data_len = cmd->pc_dma_count;
	rqst->rp_task_attr = SOP_TASK_ATTRIBUTE_SIMPLE;
	rqst->rp_id = io->io_index;
	rqst->rp_error_index = rqst->rp_id;
	(void) memcpy(rqst->rp_lun, devp->pd_scsi3addr, sizeof (rqst->rp_lun));
	(void) memcpy(rqst->rp_cdb, cmd->pc_cdb, cmd->pc_cmdlen);

	ASSERT(cmd->pc_cmdlen <= 16);
	rqst->rp_additional_cdb = SOP_ADDITIONAL_CDB_BYTES_0;

	if (cmd->pc_flags & PQI_FLAG_DMA_VALID) {
		if (cmd->pc_flags & PQI_FLAG_IO_READ)
			rqst->rp_data_dir = SOP_READ_FLAG;
		else
			rqst->rp_data_dir = SOP_WRITE_FLAG;
	} else {
		rqst->rp_data_dir = SOP_NO_DIRECTION_FLAG;
	}

	build_raid_sg_list(s, rqst, cmd, io);
	return (io);
}

/*ARGSUSED*/
pqi_cmd_t
pqi_process_comp_ring(pqi_state_t s)
{
	return (NULL);
}

static void
raid_io_complete(pqi_io_request_t *io, void *context)
{
	/*
	 * ---- XXX Not sure if this complete function will be the same
	 * or different in the end. If it's the same this will be removed
	 * and aio_io_complete will have it's named changed to something
	 * more generic.
	 */
	aio_io_complete(io, context);
}
/*ARGSUSED*/
static void
aio_io_complete(pqi_io_request_t *io, void *context)
{
	pqi_cmd_t	cmd = io->io_cmd;
	struct scsi_pkt	*pkt = CMD2PKT(cmd);

	if (cmd->pc_flags & (PQI_FLAG_IO_READ | PQI_FLAG_IO_IOPB))
		(void) ddi_dma_sync(cmd->pc_dmahdl, 0, 0, DDI_DMA_SYNC_FORCPU);

	switch (io->io_status) {
	case PQI_DATA_IN_OUT_UNDERFLOW:
		pkt->pkt_state |= STATE_GOT_BUS | STATE_GOT_TARGET |
		    STATE_SENT_CMD | STATE_GOT_STATUS;
		if (pkt->pkt_resid == cmd->pc_dma_count) {
			pkt->pkt_reason = CMD_INCOMPLETE;
		} else {
			pkt->pkt_state |= STATE_XFERRED_DATA;
			pkt->pkt_reason = CMD_CMPLT;
		}
		break;

	case PQI_DATA_IN_OUT_GOOD:
		pkt->pkt_state |= STATE_GOT_BUS | STATE_GOT_TARGET |
		    STATE_SENT_CMD | STATE_GOT_STATUS;
		if (cmd->pc_flags & PQI_FLAG_DMA_VALID)
			pkt->pkt_state |= STATE_XFERRED_DATA;
		pkt->pkt_reason = CMD_CMPLT;
		pkt->pkt_resid = 0;
		pkt->pkt_statistics = 0;
		*pkt->pkt_scbp = STATUS_GOOD;

		break;
	}

	pqi_cmd_sm(cmd, PQI_CMD_CMPLT);
	pqi_free_io(io);

	/* ---- Don't touch 'cmd' after here ---- */
	if (cmd->pc_poll)
		sema_v(cmd->pc_poll);
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0 && (pkt->pkt_comp != NULL))
		(*pkt->pkt_comp)(pkt);
}

static void
fail_outstanding_cmds(pqi_state_t s)
{
	pqi_device_t	devp;
	int		i;
	pqi_queue_group_t	*qg;

	ASSERT(MUTEX_HELD(&s->s_mutex));
	if (s->s_sync_io != NULL) {
		s->s_sync_io->io_status = PQI_DATA_IN_OUT_UNSOLICITED_ABORT;
		(s->s_sync_io->io_cb)(s->s_sync_io,
		    s->s_sync_io->io_context);
	}

	for (i = 0; i < s->s_num_queue_groups; i++) {
		qg = &s->s_queue_groups[i];
		mutex_enter(&qg->submit_lock[RAID_PATH]);
		mutex_enter(&qg->submit_lock[AIO_PATH]);
		qg->qg_active = B_FALSE;
		mutex_exit(&qg->submit_lock[AIO_PATH]);
		mutex_exit(&qg->submit_lock[RAID_PATH]);
	}

	for (devp = list_head(&s->s_devnodes); devp != NULL;
	    devp = list_next(&s->s_devnodes, devp)) {
		pqi_fail_drive_cmds(devp);
	}
}

static void
set_sg_descriptor(pqi_sg_entry_t *sg, ddi_dma_cookie_t *cookie)
{
	sg->sg_addr = cookie->dmac_laddress;
	sg->sg_len = cookie->dmac_size;
	sg->sg_flags = 0;
}

static void
build_aio_sg_list(pqi_state_t s, pqi_aio_path_request_t *rqst,
    pqi_cmd_t cmd, pqi_io_request_t *io)
{
	int			i,
				max_sg_per_iu;
	uint16_t		iu_length;
	uint8_t			chained,
				num_sg_in_iu	= 0;
	ddi_dma_cookie_t	*cookies;
	pqi_sg_entry_t		*sg;

	iu_length = offsetof(struct pqi_aio_path_request, ap_sglist) -
	    PQI_REQUEST_HEADER_LENGTH;

	if (cmd->pc_dmaccount == 0)
		goto out;
	sg = rqst->ap_sglist;
	cookies = cmd->pc_cached_cookies;
	max_sg_per_iu = s->s_max_sg_per_iu - 1;
	i = 0;
	chained = 0;

	for (;;) {
		set_sg_descriptor(sg, cookies);
		if (!chained)
			num_sg_in_iu++;
		i++;
		if (i == cmd->pc_dmaccount)
			break;
		sg++;
		cookies++;
		if (i == max_sg_per_iu) {
			sg->sg_addr = io->io_sg_chain_dma->dma_addr;
			sg->sg_len = (cmd->pc_dmaccount - num_sg_in_iu) *
			    sizeof (*sg);
			sg->sg_flags = CISS_SG_CHAIN;
			chained = 1;
			num_sg_in_iu++;
			sg = (pqi_sg_entry_t *)
			    io->io_sg_chain_dma->alloc_memory;
		}
	}
	sg->sg_flags = CISS_SG_LAST;
	rqst->partial = chained;
	if (chained) {
		(void) ddi_dma_sync(io->io_sg_chain_dma->handle, 0, 0,
		    DDI_DMA_SYNC_FORDEV);
	}
	iu_length += num_sg_in_iu * sizeof (*sg);

out:
	rqst->header.iu_length = iu_length;
	rqst->num_sg_descriptors = num_sg_in_iu;
}

static void
build_raid_sg_list(pqi_state_t s, pqi_raid_path_request_t *rqst,
    pqi_cmd_t cmd, pqi_io_request_t *io)
{
	int			i		= 0,
				max_sg_per_iu,
				num_sg_in_iu	= 0;
	uint16_t		iu_length;
	uint8_t			chained		= 0;
	ddi_dma_cookie_t	*cookies;
	pqi_sg_entry_t		*sg;

	iu_length = offsetof(struct pqi_raid_path_request, rp_sglist) -
	    PQI_REQUEST_HEADER_LENGTH;

	if (cmd->pc_dmaccount == 0)
		goto out;

	sg = rqst->rp_sglist;
	cookies = cmd->pc_cached_cookies;
	max_sg_per_iu = s->s_max_sg_per_iu - 1;

	for (;;) {
		set_sg_descriptor(sg, cookies);
		if (!chained)
			num_sg_in_iu++;
		i++;
		if (i == cmd->pc_dmaccount)
			break;
		sg++;
		cookies++;
		if (i == max_sg_per_iu) {
			ASSERT(io->io_sg_chain_dma != NULL);
			sg->sg_addr = io->io_sg_chain_dma->dma_addr;
			sg->sg_len = (cmd->pc_dmaccount - num_sg_in_iu) *
			    sizeof (*sg);
			sg->sg_flags = CISS_SG_CHAIN;
			chained = 1;
			num_sg_in_iu++;
			sg = (pqi_sg_entry_t *)
			    io->io_sg_chain_dma->alloc_memory;
		}
	}
	sg->sg_flags = CISS_SG_LAST;
	rqst->rp_partial = chained;
	if (chained) {
		(void) ddi_dma_sync(io->io_sg_chain_dma->handle, 0, 0,
		    DDI_DMA_SYNC_FORDEV);
	}
	iu_length += num_sg_in_iu * sizeof (*sg);

out:
	rqst->header.iu_length = iu_length;
}

static uint32_t
read_heartbeat_counter(pqi_state_t s)
{
	return (ddi_get32(s->s_datap, s->s_heartbeat_counter));
}

static void
take_ctlr_offline(pqi_state_t s)
{
	mutex_enter(&s->s_mutex);
	s->s_offline = 1;
	s->s_watchdog = 0;
	fail_outstanding_cmds(s);
	mutex_exit(&s->s_mutex);

	/*
	 * This will have the effect of releasing the device's dip
	 * structure from the NDI layer do to s_offline == 1.
	 */
	(void) pqi_config_all(s->s_dip, s);
}

static uint32_t
free_elem_count(pqi_index_t pi, pqi_index_t ci, uint32_t per_iq)
{
	pqi_index_t	used;
	if (pi >= ci) {
		used = pi - ci;
	} else {
		used = per_iq - ci + pi;
	}
	return (per_iq - used - 1);
}

static boolean_t
is_aio_enabled(pqi_device_t d)
{
	return (d->pd_aio_enabled ? B_TRUE : B_FALSE);
}
