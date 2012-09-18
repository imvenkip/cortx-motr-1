/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 09/07/2012
 */

#pragma once

#ifndef __COLIBRI_COLIBRI_MAGIC_H__
#define __COLIBRI_COLIBRI_MAGIC_H__

/*
 * Magic values used to tag Colibri structures.
 * Create magic numbers by referring to
 *     http://www.nsftools.com/tips/HexWords.htm
 */

enum c2_magic_satchel {
/* ADDB */
	/* c2_addb_record_header::arh_magic1 (abbadabbadoo) */
	C2_ADDB_REC_HEADER_MAGIC1 = 0x33abbadabbad0077,

	/* c2_addb_record_header::arh_magic2 (bifacial beef) */
	C2_ADDB_REC_HEADER_MAGIC2 = 0x33b1fac1a1beef77,

/* balloc */
	/* c2_balloc_super_block::bsb_magic (blessed baloc) */
	C2_BALLOC_SB_MAGIC = 0x33b1e55edba10c77,

/* c2t1fs */
	/* c2t1fs_sb::s_magic (cozie filesis) */
	C2_T1FS_SUPER_MAGIC = 0x33c021ef11e51577,

	/* c2t1fs_service_ctx::sc_magic (failed facade) */
	C2_T1FS_SVC_CTX_MAGIC = 0x33fa11edfacade77,

	/* svc_ctx_tl::td_head_magic (feedable food) */
	C2_T1FS_SVC_CTX_HEAD_MAGIC = 0x33feedab1ef00d77,

	/* c2t1fs_inode_bob::bt_magix (idolised idol) */
	C2_T1FS_INODE_MAGIC = 0x331d0115ed1d0177,

	/* c2t1fs_dir_ent::de_magic (looseleaf oil) */
	C2_T1FS_DIRENT_MAGIC = 0x331005e1eaf01177,

	/* dir_ents_tl::td_head_magic (lidless slide) */
	C2_T1FS_DIRENT_HEAD_MAGIC = 0x3311d1e55511de77,

	/* rw_desc::rd_magic (alfalfa alibi) */
	C2_T1FS_RW_DESC_MAGIC = 0x33a1fa1faa11b177,

	/* rwd_tl::td_head_magic (assail assoil) */
	C2_T1FS_RW_DESC_HEAD_MAGIC = 0x33a55a11a5501177,

	/* c2t1fs_buf::cb_magic (balled azalia) */
	C2_T1FS_BUF_MAGIC = 0x33ba11eda2a11a77,

	/* bufs_tl::td_head_magic (bedded celiac) */
	C2_T1FS_BUF_HEAD_MAGIC = 0x33beddedce11ac77,

/* Colibri Setup */
	/* cs_buffer_pool::cs_bp_magic (felicia feliz) */
	C2_CS_BUFFER_POOL_MAGIC = 0x33fe11c1afe11277,

	/* cs_buffer_pools_tl::td_head_magic (edible doodle) */
	C2_CS_BUFFER_POOL_HEAD_MAGIC = 0x33ed1b1ed00d1e77,

	/* cs_reqh_context::rc_magix (cooled coffee) */
	C2_CS_REQH_CTX_MAGIC = 0x33c001edc0ffee77,

	/* rhctx_tl::td_head_magix (abdicable ace) */
	C2_CS_REQH_CTX_HEAD_MAGIC = 0x33abd1cab1eace77,

	/* cs_endpoint_and_xprt::ex_magix (adios alibaba) */
	C2_CS_ENDPOINT_AND_XPRT_MAGIC = 0x33ad105a11baba77,

	/* cs_eps_tl::td_head_magic (felic felicis) */
	C2_CS_EPS_HEAD_MAGIC = 0x33fe11cfe11c1577,

	/* cs_ad_stob::as_magix (ecobabble elf) */
	C2_CS_AD_STOB_MAGIC = 0x33ec0babb1ee1f77,

	/* astob::td_head_magic (foodless flee) */
	C2_CS_AD_STOB_HEAD_MAGIC = 0x33f00d1e55f1ee77,

	/* ndom_tl::td_head_magic (baffled basis) */
	C2_CS_NET_DOMAIN_HEAD_MAGIC = 0x33baff1edba51577,

/* desim */
	/* client_write_ext::cwe_magic (abasic access) */
	C2_DESIM_CLIENT_WRITE_EXT_MAGIC = 0x33aba51cacce5577,

	/* cl_tl::td_head_magic (abscessed ace) */
	C2_DESIM_CLIENT_WRITE_EXT_HEAD_MAGIC = 0x33ab5ce55edace77,

	/* cnt::c_magic (al azollaceae) */
	C2_DESIM_CNT_MAGIC = 0x33a1a2011aceae77,

	/* cnts_tl::td_head_magic (biased balzac) */
	C2_DESIM_CNTS_HEAD_MAGIC = 0x33b1a5edba12ac77,

	/* io_req::ir_magic (biblical bias) */
	C2_DESIM_IO_REQ_MAGIC = 0x33b1b11ca1b1a577,

	/* req_tl::td_head_magic (bifolded case) */
	C2_DESIM_IO_REQ_HEAD_MAGIC = 0x33b1f01dedca5e77,

	/* net_rpc::nr_magic (das classless) */
	C2_DESIM_NET_RPC_MAGIC = 0x33da5c1a551e5577,

	/* rpc_tl::td_head_magic (delible diazo) */
	C2_DESIM_NET_RPC_HEAD_MAGIC = 0x33de11b1ed1a2077,

	/* sim_callout::sc_magic (escalade fall) */
	C2_DESIM_SIM_CALLOUT_MAGIC = 0x33e5ca1adefa1177,

	/* ca_tl::td_head_magic (leaded lescol) */
	C2_DESIM_SIM_CALLOUT_HEAD_MAGIC = 0x331eaded1e5c0177,

	/* sim_callout::sc_magic (odessa saddle) */
	C2_DESIM_SIM_THREAD_MAGIC = 0x330de55a5add1e77,

	/* ca_tl::td_head_magic (scaffold sale) */
	C2_DESIM_SIM_THREAD_HEAD_MAGIC = 0x335caff01d5a1e77,

/* DB */
	/* c2_db_tx_waiter::tw_magix (ascii salidas) */
	C2_DB_TX_WAITER_MAGIC = 0x33a5c115a11da577,

	/* enw_tl::td_head_magic (dazed coccidi) */
	C2_DB_TX_WAITER_HEAD_MAGIC = 0x33da2edc0cc1d177,

/* Fault Injection */
	/* fi_dynamic_id::fdi_magic (diabolic dill) */
	C2_FI_DYNAMIC_ID_MAGIC = 0x33d1ab011cd11177,

	/* fi_dynamic_id_tl::td_head_magic (decoded diode) */
	C2_FI_DYNAMIC_ID_HEAD_MAGIC = 0x33dec0dedd10de77,

/* FOP */
	/* c2_fop_type::ft_magix (balboa saddle) */
	C2_FOP_TYPE_MAGIC = 0x33ba1b0a5add1e77,

	/* fop_types_list::t_magic (baffle bacili) */
	C2_FOP_TYPE_HEAD_MAGIC = 0x33baff1ebac11177,

	/* c2_fom_thread::lt_magix (falsifiable C) */
	C2_FOM_THREAD_MAGIC = 0x33fa151f1ab1ec77,

	/* thr_tl::td_head_magic (declassified) */
	C2_FOM_THREAD_HEAD_MAGIC = 0x33dec1a551f1ed77,

	/* c2_long_lock_link::lll_magix (idealised ice) */
	C2_FOM_LL_LINK_MAGIC = 0x331dea115ed1ce77,

	/* c2_long_lock::l_magix (blessed boss) */
	C2_FOM_LL_MAGIC = 0x330b1e55edb05577,

/* ioservice */
	/* c2_cobfid_map::cfm_magic (zozofied ziff) */
	C2_CFM_MAP_MAGIC = 0x332020f1ed21ff77,

	/* c2_cobfid_map_iter::cfmi_magic (clab cocobogo) */
	C2_CFM_ITER_MAGIC = 0x33c1abc0c0b09077,

	/* c2_stob_io_descr::siod_linkage (zealos obsses) */
	C2_STOB_IO_DESC_LINK_MAGIC = 0x332ea1050b55e577,

	/* stobio_tl::td_head_magic (official ball) */
	C2_STOB_IO_DESC_HEAD_MAGIC = 0x330ff1c1a1ba1177,

	/* netbufs_tl::td_head_magic (fiscal diesel ) */
	C2_IOS_NET_BUFFER_HEAD_MAGIC = 0x33f15ca1d1e5e177,

	/* c2_reqh_io_service::rios_magic (cocigeal cell) */
	C2_IOS_REQH_SVC_MAGIC = 0x33c0c19ea1ce1177,

	/* bufferpools_tl::rios_bp_magic (cafe accolade) */
	C2_IOS_BUFFER_POOL_MAGIC = 0x33cafeacc01ade77,

	/* bufferpools_tl::td_head_magic (colossal face) */
	C2_IOS_BUFFER_POOL_HEAD_MAGIC = 0x33c01055a1face77,

	/* c2_io_fop::if_magic (affable aided) */
	C2_IO_FOP_MAGIC = 0x33affab1ea1ded77,

	/* ioseg::is_magic (soleless zeal) */
	C2_IOS_IO_SEGMENT_MAGIC = 0x33501e1e552ea177,

	/* iosegset::td_head_magic (doddle fascia) */
	C2_IOS_IO_SEGMENT_SET_MAGIC = 0x33d0dd1efa5c1a77,

/* Layout */
	/* c2_layout::l_magic (edible sassie) */
	C2_LAYOUT_MAGIC = 0x33ed1b1e5a551e77,

	/* c2_layout_enum::le_magic (ideal follies) */
	C2_LAYOUT_ENUM_MAGIC = 0x331dea1f0111e577,

	/* layout_tlist::head_magic (biddable blad) */
	C2_LAYOUT_HEAD_MAGIC = 0x33b1ddab1eb1ad77,

	/* c2_layout_instance::li_magic (cicilial cell) */
	C2_LAYOUT_INSTANCE_MAGIC = 0x33c1c111a1ce1177,

	/* c2_pdclust_layout::pl_magic (balolo ballio) */
	C2_LAYOUT_PDCLUST_MAGIC = 0x33ba1010ba111077,

	/* c2_pdclust_instance::pi_magic (de coccolobis) */
	C2_LAYOUT_PDCLUST_INSTANCE_MAGIC = 0x33dec0cc010b1577,

	/* c2_layout_list_enum::lle_magic (ofella soiled) */
	C2_LAYOUT_LIST_ENUM_MAGIC = 0x330fe11a5011ed77,

	/* c2_layout_linear_enum::lla_magic (boldface blob) */
	C2_LAYOUT_LINEAR_ENUM_MAGIC = 0x33b01dfaceb10b77,

/* Net */
	/* c2_net_domain::nd_magix (acidic access) */
	C2_NET_DOMAIN_MAGIC = 0x33ac1d1cacce5577,

	/* ndom_tl::td_head_magic (adelaide aide) */
	C2_NET_DOMAIN_HEAD_MAGIC = 0x33ade1a1dea1de77,

	/* netbufs_tl::td_head_magic (saleable sale) */
	C2_NET_BUFFER_HEAD_MAGIC = 0x335a1eab1e5a1e77,

	/* c2_net_buffer::nb_tm_linkage (social silica) */
	C2_NET_BUFFER_LINK_MAGIC = 0x3350c1a15111ca77,

	/* c2_net_pool_tl::td_head_magic (zodiacal feed) */
	C2_NET_POOL_HEAD_MAGIC = 0x3320d1aca1feed77,

	/* c2_net_bulk_mem_end_point::xep_magic (bedside flood) */
	C2_NET_BULK_MEM_XEP_MAGIC = 0x33bed51def100d77,

	/* nlx_kcore_domain::kd_magic (classical cob) */
	C2_NET_LNET_KCORE_DOM_MAGIC = 0x33c1a551ca1c0b77,

	/* nlx_kcore_transfer_mc::ktm_magic (eggless abode) */
	C2_NET_LNET_KCORE_TM_MAGIC  = 0x33e991e55ab0de77,

	/* tms_tl::td_head_magic (alfaa bacilli) */
	C2_NET_LNET_KCORE_TMS_MAGIC = 0x33a1faabac111177,

	/* nlx_kcore_buffer::kb_magic (dissociablee) */
	C2_NET_LNET_KCORE_BUF_MAGIC = 0x33d1550c1ab1ee77,

	/* nlx_kcore_buffer_event::bev_magic (salsa bacilli) */
	C2_NET_LNET_KCORE_BEV_MAGIC = 0x335a15abac111177,

	/* drv_tms_tl::td_head_magic (cocci bacilli) */
	C2_NET_LNET_DEV_TMS_MAGIC   = 0x33c0cc1bac111177,

	/* drv_bufs_tl::td_head_magic (cicadellidae) */
	C2_NET_LNET_DEV_BUFS_MAGIC  = 0x33c1cade111dae77,

	/* drv_bevs_tl::td_head_magic (le cisco disco) */
	C2_NET_LNET_DEV_BEVS_MAGIC  = 0x331ec15c0d15c077,

	/* nlx_ucore_domain::ud_magic (blooded blade) */
	C2_NET_LNET_UCORE_DOM_MAGIC = 0x33b100dedb1ade77,

	/* nlx_ucore_transfer_mc::utm_magic (obsessed lila) */
	C2_NET_LNET_UCORE_TM_MAGIC  = 0x330b5e55ed111a77,

	/* nlx_ucore_buffer::ub_magic (ideal icefall) */
	C2_NET_LNET_UCORE_BUF_MAGIC = 0x331dea11cefa1177,

	/* nlx_core_buffer::cb_magic (edible icicle) */
	C2_NET_LNET_CORE_BUF_MAGIC = 0x33ed1b1e1c1c1e77,

	/* nlx_core_transfer_mc::ctm_magic (focal edifice) */
	C2_NET_LNET_CORE_TM_MAGIC  = 0x33f0ca1ed1f1ce77,

	/* nlx_xo_ep::xe_magic (failed fiasco) */
	C2_NET_LNET_XE_MAGIC = 0x33fa11edf1a5c077,

/* Request handler */
	/* c2_reqh_service::rs_magix (bacilli babel) */
	C2_REQH_SVC_MAGIC = 0x33bac1111babe177,

	/* c2_reqh_service_type::rst_magix (fiddless cobe) */
	C2_REQH_SVC_TYPE_MAGIC = 0x33f1dd1e55c0be77,

	/* c2_reqh_svc_tl::td_head_magic (calcific boss) */
	C2_REQH_SVC_HEAD_MAGIC = 0x33ca1c1f1cb05577,

	/* c2_reqh_rpc_mach_tl::td_head_magic (laissez eifel) */
	C2_REQH_RPC_MACH_HEAD_MAGIC = 0x331a155e2e1fe177,

/* RPC */
	/* c2_rpc_service_type::svt_magix (seedless seel) */
	C2_RPC_SERVICE_TYPE_MAGIC = 0x335eed1e555ee177,

	/* c2_rpc_service::svc_magix (selfless self) */
	C2_RPC_SERVICE_MAGIC = 0x335e1f1e555e1f77,

	/* c2_rpc_services_tl::td_head_magic (lillie lisboa) */
	C2_RPC_SERVICES_HEAD_MAGIC = 0x3311111e115b0a77,

	/* rpc_service_types_tl::td_head_magic (fosilised foe) */
	C2_RPC_SERVICE_TYPES_HEAD_MAGIC = 0x33f055115edf0e77,

	/* c2_rpc_bulk_buf::bb_link (lidded liliac) */
	C2_RPC_BULK_BUF_MAGIC = 0x3311dded1111ac77,

	/* c2_rpc_bulk::rb_magic (leafless idol) */
	C2_RPC_BULK_MAGIC = 0x331eaf1e551d0177,

	/* c2_rpc_frm::f_magic (adelice dobie) */
	C2_RPC_FRM_MAGIC = 0x33ade11ced0b1e77,

	/* itemq_tl::td_head_magic (dazzled cliff) */
	C2_RPC_ITEMQ_HEAD_MAGIC = 0x33da221edc11ff77,

	/* c2_rpc_item::ri_field (boiled coolie) */
	C2_RPC_ITEM_MAGIC = 0x33b011edc0011e77,

	/* rpcitem_tl::td_head_magic (disabled disc) */
	C2_RPC_ITEM_HEAD_MAGIC = 0x33d15ab1edd15c77,

	/* rpc_buffer::rb_magic (iodized isaac) */
	C2_RPC_BUF_MAGIC = 0x3310d12ed15aac77,

	/* c2_rpc_machine::rm_magix (deboise aloof) */
	C2_RPC_MACHINE_MAGIC = 0x33deb015ea100f77,

	/* c2_rpc_item_type::rit_magic (daffodil dace) */
	C2_RPC_ITEM_TYPE_MAGIC = 0x33daff0d11dace77,

	/* rit_tl::td_head_magic (caboodle cold) */
	C2_RPC_ITEM_TYPE_HEAD_MAGIC = 0x33cab00d1ec01d77,

	/* packet_item_tl::td_head_magic (falloff eagle) */
	C2_RPC_PACKET_HEAD_MAGIC = 0x33fa110ffea91e77,

	/* c2_rpc_conn::c_magic (classic alibi) */
	C2_RPC_CONN_MAGIC = 0x33c1a551ca11b1,

	/* rpc_conn_tl::td_head_magic (bloodless god) */
	C2_RPC_CONN_HEAD_MAGIC = 0x33b100d1e5590d,

/* Trace */
	/* c2_trace_rec_header::trh_magic (foldable doll) */
	C2_TRACE_MAGIC = 0x33f01dab1ed01177,
};

#endif /* __COLIBRI_COLIBRI_MAGIC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
