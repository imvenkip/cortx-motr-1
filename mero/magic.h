/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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

#ifndef __MERO_MERO_MAGIC_H__
#define __MERO_MERO_MAGIC_H__

/*
 * Magic values used to tag Mero structures.
 * Create magic numbers by referring to
 *     http://www.nsftools.com/tips/HexWords.htm
 */

enum m0_magic_satchel {
/* ADDB */
	/* m0_addb_counter::acn_magic (addb add beads) */
	M0_ADDB_CNTR_MAGIC = 0x33addbaddbead577,

	/* m0_addb_ctx::ac_magic (addb addb cooo) */
	M0_ADDB_CTX_MAGIC = 0x33addbaddbc00077,

	/* m0_addb_ctx_type::act_magic (addb addb cabe) */
	M0_ADDB_CT_MAGIC = 0x33addbaddbcabe77,

	/* CT list head magic (addb addb casa) */
	M0_ADDB_CT_HEAD_MAGIC = 0x33addbaddbca5a77,

	/* TS header tlist head magic (addb addb sail) */
	M0_ADDB_TS_HEAD_MAGIC = 0x33addbaddb5a1177,

	/* TS header tlist link magic (addb addb seal) */
	M0_ADDB_TS_LINK_MAGIC = 0x33addbaddb5ea177,

	/* m0_addb_ts_rec:atr_magic (addb addb seel) */
	M0_ADDB_TS_REC_MAGIC = 0x33addbaddb5ee177,
	/* caching m0_addb_mc_evmgr::evm_magic (addb coccidae) */
	M0_ADDB_CACHE_EVMGR_MAGIC = 0x33addbc0cc1dae77,

	/* passthrough m0_addb_mc_evmgr::evm_magic (addb escalade) */
	M0_ADDB_PT_EVMGR_MAGIC = 0x33addbe5ca1ade77,

	/* m0_addb_rec_type::art_magic (addb addb aiee) */
	M0_ADDB_RT_MAGIC = 0x33addbaddba1ee77,

	/* RT list head magic (addb addb dada) */
	M0_ADDB_RT_HEAD_MAGIC = 0x33addbaddbdada77,

	/* m0_addb_record_header::arh_magic1 (abbadabbadoo) */
	M0_ADDB_REC_HEADER_MAGIC1 = 0x33abbadabbad0077,

	/* m0_addb_mc::am_magic (add boo facade) */
	M0_ADDB_MC_MAGIC = 0x33addb00facade77,

	/* m0_addb_record_header::arh_magic2 (bifacial beef) */
	M0_ADDB_REC_HEADER_MAGIC2 = 0x33b1fac1a1beef77,

	/* addb_ctx_def_cache::acdc_magic (addb cdc field) */
	M0_ADDB_CDC_MAGIC = 0x33addbcdcf1e1d77,

	/* addb_cdc list head magic (addb cdc 0 base) */
	M0_ADDB_CDC_HEAD_MAGIC = 0x33addbcdc0ba5e77,

	/* addb_post_fom magic (saddle sleaze)  */
	M0_ADDB_PFOM_MAGIC = 0x335add1e51ea2e77,

	/* addb_svc magic (0 sozzled slob)  */
	M0_ADDB_SVC_MAGIC = 0x33050221ed510b77,

	/* stobsink_ops::rs_magic (addable addle) */
	M0_ADDB_STOBSINK_MAGIC = 0x33addab1eadd1e77,

	/* stobsink_poolbuf::spb_magic (addable babel) */
	M0_ADDB_STOBSINK_BUF_MAGIC = 0x33addab1ebabe177,

	/* stob_segment_iter magic (addb seize elf) */
	M0_ADDB_STOBRET_MAGIC = 0x33addb5e12ee1f77,

	/* file_segment_iter magic (addb file sale) */
	M0_ADDB_FILERET_MAGIC = 0x33addbf11e5a1e77,

	/* rpcsink::rs_magic (addb safe safe) */
	M0_ADDB_RPCSINK_MAGIC = 0x33addb5afe5afe77,

	/* rpcsink_fop::rf_magic (addb safe foil) */
	M0_ADDB_RPCSINK_FOP_MAGIC = 0x33addb5afef01177,

	/* rpcsink_item_source::ris_magic (addb safe ie so) */
	M0_ADDB_RPCSINK_ITEM_SOURCE_MAGIC = 0x33addb5afe1e5077,

	/* rpcsink records submitted list head (addb safe slab) */
	M0_ADDB_RPCSINK_TS_HEAD_MAGIC1 =0x33addb5afe51ab77,

	/* rpcsink records ready to send list head (addb safe sled) */
	M0_ADDB_RPCSINK_TS_HEAD_MAGIC2 =0x33addb5afe51ed77,

	/* rpcsink item source list head (addb safe sloe) */
	M0_ADDB_RPCSINK_IS_HEAD_MAGIC =0x33addb5afe510e77,

/* balloc */
	/* m0_balloc_super_block::bsb_magic (blessed baloc) */
	M0_BALLOC_SB_MAGIC = 0x33b1e55edba10c77,

/* be */
	/* XXX TODO generate it. be_alloc_chunk::bac_magic0 */
	M0_BE_ALLOC_MAGIC0 = 0x330123456789AB77,
	/* XXX TODO generate it. be_alloc_chunk::bac_magic1 */
	M0_BE_ALLOC_MAGIC1 = 0x33BA987654321077,
	/* XXX TODO generate it. m0_be_allocator_header::bah_chunks */
	M0_BE_ALLOC_ALL_MAGIC = 0x3300112233445577,
	/* XXX TODO generate it. be_alloc_chunk::bac_magic */
	M0_BE_ALLOC_ALL_LINK_MAGIC = 0x3355443322110077,
	/* XXX TODO generate it. m0_be_allocator_header::bah_free */
	M0_BE_ALLOC_FREE_MAGIC = 0x3300001111222277,
	/* XXX TODO generate it. be_alloc_chunk::bac_magic_free */
	M0_BE_ALLOC_FREE_LINK_MAGIC = 0x3322221111000077,

/* m0t1fs */
	/* m0t1fs_sb::s_magic (cozie filesis) */
	M0_T1FS_SUPER_MAGIC = 0x33c021ef11e51577,

	/* m0t1fs_service_ctx::sc_magic (failed facade) */
	M0_T1FS_SVC_CTX_MAGIC = 0x33fa11edfacade77,

	/* svc_ctx_tl::td_head_magic (feedable food) */
	M0_T1FS_SVC_CTX_HEAD_MAGIC = 0x33feedab1ef00d77,

	/* m0t1fs_inode_bob::bt_magix (idolised idol) */
	M0_T1FS_INODE_MAGIC = 0x331d0115ed1d0177,

	/* m0t1fs_dir_ent::de_magic (looseleaf oil) */
	M0_T1FS_DIRENT_MAGIC = 0x331005e1eaf01177,

	/* dir_ents_tl::td_head_magic (lidless slide) */
	M0_T1FS_DIRENT_HEAD_MAGIC = 0x3311d1e55511de77,

	/* rw_desc::rd_magic (alfalfa alibi) */
	M0_T1FS_RW_DESC_MAGIC = 0x33a1fa1faa11b177,

	/* rwd_tl::td_head_magic (assail assoil) */
	M0_T1FS_RW_DESC_HEAD_MAGIC = 0x33a55a11a5501177,

	/* m0t1fs_buf::cb_magic (balled azalia) */
	M0_T1FS_BUF_MAGIC = 0x33ba11eda2a11a77,

	/* bufs_tl::td_head_magic (bedded celiac) */
	M0_T1FS_BUF_HEAD_MAGIC = 0x33beddedce11ac77,

        /* io_request::ir_magic (fearsome acts) */
        M0_T1FS_IOREQ_MAGIC  = 0x33fea2503eac1577,

        /* nw_xfer_request::nxr_magic (coffee arabic) */
        M0_T1FS_NWREQ_MAGIC  = 0x33c0ffeea2ab1c77,

        /* target_ioreq::ti_magic (falafel bread) */
        M0_T1FS_TIOREQ_MAGIC = 0x33fa1afe1b2ead77,

        /* io_req_fop::irf_magic (desirability) */
        M0_T1FS_IOFOP_MAGIC  = 0x33de512ab1111777,

        /* data_buf::db_magic (fire incoming) */
        M0_T1FS_DTBUF_MAGIC  = 0x33f12e19c0319977,

        /* pargrp_iomap::pi_magic (incandescent) */
        M0_T1FS_PGROUP_MAGIC = 0x3319ca9de5ce9177,

	/* m0t1fs_services_tl::mss_magic (baseless bass) */
	M0_T1FS_SERVICE_MAGIC = 0x33ba5e1e55ba5577,

	/* m0t1fs_services_tl::td_head_magic (seize sicilia) */
	M0_T1FS_SERVICE_HEAD_MAGIC =0x335e12e51c111a77,

	/* hashbucket::hb_tioreqs::td_head_magic = desirability */
	M0_T1FS_TLIST_HEAD_MAGIC = 0x33de512ab1111777,

/* Configuration */
	/* m0_conf_cache::ca_registry::t_magic (fabled feodal) */
	M0_CONF_CACHE_MAGIC = 0x33fab1edfe0da177,

	/* m0_conf_obj::co_gen_magic (selfless cell) */
	M0_CONF_OBJ_MAGIC = 0x335e1f1e55ce1177,

	/* m0_conf_dir::cd_obj.co_con_magic (old calaboose) */
	M0_CONF_DIR_MAGIC = 0x3301dca1ab005e77,

	/* m0_conf_profile::cp_obj.co_con_magic (closable seal) */
	M0_CONF_PROFILE_MAGIC = 0x33c105ab1e5ea177,

	/* m0_conf_filesystem::cf_obj.co_con_magic (food of Colaba) */
	M0_CONF_FILESYSTEM_MAGIC = 0x33f00d0fc01aba77,

	/* m0_conf_service::cs_obj.co_con_magic (biased locale) */
	M0_CONF_SERVICE_MAGIC = 0x33b1a5ed10ca1e77,

	/* m0_conf_node::cn_obj.co_con_magic (colossal dosa) */
	M0_CONF_NODE_MAGIC = 0x33c01055a1d05a77,

	/* m0_conf_nic::ni_obj.co_con_magic (baseball feed) */
	M0_CONF_NIC_MAGIC = 0x33ba5eba11feed77,

	/* m0_conf_sdev::sd_obj.co_con_magic (allseed salad) */
	M0_CONF_SDEV_MAGIC = 0x33a115eed5a1ad77,

	/* m0_conf_partition::pa_obj.co_con_magic (bacca is aloof)
	 * Let's hope we won't be sued by Symantec for this name. */
	M0_CONF_PARTITION_MAGIC = 0x33bacca15a100f77,

	/* m0_confc::cc_magic (zodiac doable) */
	M0_CONFC_MAGIC = 0x3320d1acd0ab1e77,

	/* m0_confc_ctx::fc_magic (ablaze filial) */
	M0_CONFC_CTX_MAGIC = 0x33ab1a2ef111a177,

	/* m0_confd::c_magic (isolable lasi) */
	M0_CONFD_MAGIC = 0x331501ab1e1a5177,

/* Mero Setup */
	/* cs_buffer_pool::cs_bp_magic (felicia feliz) */
	M0_CS_BUFFER_POOL_MAGIC = 0x33fe11c1afe11277,

	/* cs_buffer_pools_tl::td_head_magic (edible doodle) */
	M0_CS_BUFFER_POOL_HEAD_MAGIC = 0x33ed1b1ed00d1e77,

	/* cs_reqh_context::rc_magix (cooled coffee) */
	M0_CS_REQH_CTX_MAGIC = 0x33c001edc0ffee77,

	/* rhctx_tl::td_head_magix (abdicable ace) */
	M0_CS_REQH_CTX_HEAD_MAGIC = 0x33abd1cab1eace77,

	/* cs_endpoint_and_xprt::ex_magix (adios alibaba) */
	M0_CS_ENDPOINT_AND_XPRT_MAGIC = 0x33ad105a11baba77,

	/* cs_eps_tl::td_head_magic (felic felicis) */
	M0_CS_EPS_HEAD_MAGIC = 0x33fe11cfe11c1577,

	/* cs_ad_stob::as_magix (ecobabble elf) */
	M0_CS_AD_STOB_MAGIC = 0x33ec0babb1ee1f77,

	/* astob::td_head_magic (foodless flee) */
	M0_CS_AD_STOB_HEAD_MAGIC = 0x33f00d1e55f1ee77,

	/* ndom_tl::td_head_magic (baffled basis) */
	M0_CS_NET_DOMAIN_HEAD_MAGIC = 0x33baff1edba51577,

/* Copy machine */
	/* cmtypes_tl::td_head_magic (dacefacebace) */
	CM_TYPE_HEAD_MAGIX = 0x33DACEFACEBACE77,

	/* m0_cm_type::ct_magix (badedabadebe) */
	CM_TYPE_LINK_MAGIX = 0x33BADEDABADEBE77,

	/* cm_ag_tl::td_head_magic (deafbeefdead) */
	CM_AG_HEAD_MAGIX = 0x33DEAFBEEFDEAD77,

	/* m0_cm_aggr_group::cag_magic (feedbeefdeed) */
	CM_AG_LINK_MAGIX = 0x33FEEDBEEFDEED77,

/* Copy packet */
	/* m0_cm_cp::cp_bob (ecobabble ace) */
	CM_CP_MAGIX = 0x33ec0babb1eace77,

	/* m0_cm_cp::c_buffers (deadfoodbaad) */
	CM_CP_DATA_BUF_HEAD_MAGIX = 0x33DEADF00DBAAD77,

	/* px_pending_cps::td_head_magic () */
	CM_PROXY_CP_HEAD_MAGIX = 0x33C001F001B00177,

/* Copy machine proxy */
        /* m0_cm_proxy_tl::td_head_magic (caadbaadfaad) */
	CM_PROXY_HEAD_MAGIC = 0x33CAADBAADFAAD77,

	/* m0_cm_proxy::px_magic (C001D00DF00D) */
	CM_PROXY_LINK_MAGIC = 0x33C001D00DF00D77,

/* desim */
	/* client_write_ext::cwe_magic (abasic access) */
	M0_DESIM_CLIENT_WRITE_EXT_MAGIC = 0x33aba51cacce5577,

	/* cl_tl::td_head_magic (abscessed ace) */
	M0_DESIM_CLIENT_WRITE_EXT_HEAD_MAGIC = 0x33ab5ce55edace77,

	/* cnt::c_magic (al azollaceae) */
	M0_DESIM_CNT_MAGIC = 0x33a1a2011aceae77,

	/* cnts_tl::td_head_magic (biased balzac) */
	M0_DESIM_CNTS_HEAD_MAGIC = 0x33b1a5edba12ac77,

	/* io_req::ir_magic (biblical bias) */
	M0_DESIM_IO_REQ_MAGIC = 0x33b1b11ca1b1a577,

	/* req_tl::td_head_magic (bifolded case) */
	M0_DESIM_IO_REQ_HEAD_MAGIC = 0x33b1f01dedca5e77,

	/* net_rpc::nr_magic (das classless) */
	M0_DESIM_NET_RPC_MAGIC = 0x33da5c1a551e5577,

	/* rpc_tl::td_head_magic (delible diazo) */
	M0_DESIM_NET_RPC_HEAD_MAGIC = 0x33de11b1ed1a2077,

	/* sim_callout::sc_magic (escalade fall) */
	M0_DESIM_SIM_CALLOUT_MAGIC = 0x33e5ca1adefa1177,

	/* ca_tl::td_head_magic (leaded lescol) */
	M0_DESIM_SIM_CALLOUT_HEAD_MAGIC = 0x331eaded1e5c0177,

	/* sim_callout::sc_magic (odessa saddle) */
	M0_DESIM_SIM_THREAD_MAGIC = 0x330de55a5add1e77,

	/* ca_tl::td_head_magic (scaffold sale) */
	M0_DESIM_SIM_THREAD_HEAD_MAGIC = 0x335caff01d5a1e77,

/* DB */
	/* m0_db_tx_waiter::tw_magix (ascii salidas) */
	M0_DB_TX_WAITER_MAGIC = 0x33a5c115a11da577,

	/* enw_tl::td_head_magic (dazed coccidi) */
	M0_DB_TX_WAITER_HEAD_MAGIC = 0x33da2edc0cc1d177,

/* Fault Injection */
	/* fi_dynamic_id::fdi_magic (diabolic dill) */
	M0_FI_DYNAMIC_ID_MAGIC = 0x33d1ab011cd11177,

	/* fi_dynamic_id_tl::td_head_magic (decoded diode) */
	M0_FI_DYNAMIC_ID_HEAD_MAGIC = 0x33dec0dedd10de77,

/* FOP */
	/* m0_fop_type::ft_magix (balboa saddle) */
	M0_FOP_TYPE_MAGIC = 0x33ba1b0a5add1e77,

	/* fop_types_list::t_magic (baffle bacili) */
	M0_FOP_TYPE_HEAD_MAGIC = 0x33baff1ebac11177,

	/* m0_fom::fo_magic (leadless less) */
	M0_FOM_MAGIC = 0x331ead1e551e5577,

	/* m0_fom_locality::fl_runq::td_head_magic (alas albizzia) */
	M0_FOM_RUNQ_MAGIC = 0x33a1a5a1b1221a77,

	/* m0_fom_locality::fl_wail::td_head_magic (baseless bole) */
	M0_FOM_WAIL_MAGIC = 0x33ba5e1e55501e77,

	/* m0_fom_thread::lt_magix (falsifiable C) */
	M0_FOM_THREAD_MAGIC = 0x33fa151f1ab1ec77,

	/* thr_tl::td_head_magic (declassified) */
	M0_FOM_THREAD_HEAD_MAGIC = 0x33dec1a551f1ed77,

	/* m0_long_lock_link::lll_magix (idealised ice) */
	M0_FOM_LL_LINK_MAGIC = 0x331dea115ed1ce77,

	/* m0_long_lock::l_magix (blessed boss) */
	M0_FOM_LL_MAGIC = 0x330b1e55edb05577,

/* FOL */
	/* m0_fol_rec_part:rp_link (ceaseless deb) */
	M0_FOL_REC_PART_LINK_MAGIC = 0x33cea5e1e55deb77,
	/* m0_fol_rec_part:rp_magic (bloodied bozo) */
	M0_FOL_REC_PART_HEAD_MAGIC = 0x33b100d1edb02077,
	/* m0_fol_rec_part_header:rph_magic (baseball aced) */
	M0_FOL_REC_PART_MAGIC = 0x33ba5eba11aced77,

/* HA */
	/* m0_ha_epoch_monitor::ham_magic (bead Adelaide) */
	M0_HA_EPOCH_MONITOR_MAGIC = 0x33beadade1a1de77,
	/* m0_ha_domain::hdo_monitors::t_magic (beef official) */
	M0_HA_DOMAIN_MAGIC = 0x33beef0ff1c1a177,

/* ioservice */
	/* m0_stob_io_descr::siod_linkage (zealos obsses) */
	M0_STOB_IO_DESC_LINK_MAGIC = 0x332ea1050b55e577,

	/* stobio_tl::td_head_magic (official ball) */
	M0_STOB_IO_DESC_HEAD_MAGIC = 0x330ff1c1a1ba1177,

	/* netbufs_tl::td_head_magic (fiscal diesel ) */
	M0_IOS_NET_BUFFER_HEAD_MAGIC = 0x33f15ca1d1e5e177,

	/* m0_reqh_io_service::rios_magic (cocigeal cell) */
	M0_IOS_REQH_SVC_MAGIC = 0x33c0c19ea1ce1177,

	/* m0_reqh_md_service::rmds_magic (abscissa cell) */
	M0_MDS_REQH_SVC_MAGIC = 0x33ab5c155ace1177,

	/* bufferpools_tl::rios_bp_magic (cafe accolade) */
	M0_IOS_BUFFER_POOL_MAGIC = 0x33cafeacc01ade77,

	/* bufferpools_tl::td_head_magic (colossal face) */
	M0_IOS_BUFFER_POOL_HEAD_MAGIC = 0x33c01055a1face77,

	/* m0_io_fop::if_magic (affable aided) */
	M0_IO_FOP_MAGIC = 0x33affab1ea1ded77,

	/* ioseg::is_magic (soleless zeal) */
	M0_IOS_IO_SEGMENT_MAGIC = 0x33501e1e552ea177,

	/* iosegset::td_head_magic (doddle fascia) */
	M0_IOS_IO_SEGMENT_SET_MAGIC = 0x33d0dd1efa5c1a77,

/* Layout */
	/* m0_layout::l_magic (edible sassie) */
	M0_LAYOUT_MAGIC = 0x33ed1b1e5a551e77,

	/* m0_layout_enum::le_magic (ideal follies) */
	M0_LAYOUT_ENUM_MAGIC = 0x331dea1f0111e577,

	/* layout_tlist::head_magic (biddable blad) */
	M0_LAYOUT_HEAD_MAGIC = 0x33b1ddab1eb1ad77,

	/* m0_layout_instance::li_magic (cicilial cell) */
	M0_LAYOUT_INSTANCE_MAGIC = 0x33c1c111a1ce1177,

	/* m0_pdclust_layout::pl_magic (balolo ballio) */
	M0_LAYOUT_PDCLUST_MAGIC = 0x33ba1010ba111077,

	/* m0_pdclust_instance::pi_magic (de coccolobis) */
	M0_LAYOUT_PDCLUST_INSTANCE_MAGIC = 0x33dec0cc010b1577,

	/* m0_layout_list_enum::lle_magic (ofella soiled) */
	M0_LAYOUT_LIST_ENUM_MAGIC = 0x330fe11a5011ed77,

	/* m0_layout_linear_enum::lla_magic (boldface blob) */
	M0_LAYOUT_LINEAR_ENUM_MAGIC = 0x33b01dfaceb10b77,

/* MGMT */
	/* mgmt_svc::ms_magic (bald dada casa) */
	M0_MGMT_SVC_MAGIC = 0x33ba1ddadaca5a77,

	/* mgmt_fop_ss_fom_bob (obese seafood) */
	M0_MGMT_FOP_SS_FOM_MAGIC = 0x330be5e5eaf00d77,

	/* mgmt_fop_run_fom_bob (obese caboose) */
	M0_MGMT_FOP_RUN_FOM_MAGIC = 0x330be5ecab005e77,

	/* m0_mgmt_svc_conf::msc_magic (calico saddle) */
	M0_MGMT_SVC_CONF_MAGIC = 0x33ca11c05add1e77,

	/* m0_mgmt_node_conf::mnc_svc magic (calico lilacs) */
	M0_MGMT_NODE_CONF_MAGIC = 0x33ca11c0111ac577,

/* Net */
	/* m0_net_domain::nd_magix (acidic access) */
	M0_NET_DOMAIN_MAGIC = 0x33ac1d1cacce5577,

	/* ndom_tl::td_head_magic (adelaide aide) */
	M0_NET_DOMAIN_HEAD_MAGIC = 0x33ade1a1dea1de77,

	/* netbufs_tl::td_head_magic (saleable sale) */
	M0_NET_BUFFER_HEAD_MAGIC = 0x335a1eab1e5a1e77,

	/* m0_net_buffer::nb_tm_linkage (social silica) */
	M0_NET_BUFFER_LINK_MAGIC = 0x3350c1a15111ca77,

	/* m0_net_pool_tl::td_head_magic (zodiacal feed) */
	M0_NET_POOL_HEAD_MAGIC = 0x3320d1aca1feed77,

	/* m0_net_bulk_mem_end_point::xep_magic (bedside flood) */
	M0_NET_BULK_MEM_XEP_MAGIC = 0x33bed51def100d77,

	/* nlx_kcore_domain::kd_magic (classical cob) */
	M0_NET_LNET_KCORE_DOM_MAGIC = 0x33c1a551ca1c0b77,

	/* nlx_kcore_transfer_mc::ktm_magic (eggless abode) */
	M0_NET_LNET_KCORE_TM_MAGIC  = 0x33e991e55ab0de77,

	/* tms_tl::td_head_magic (alfaa bacilli) */
	M0_NET_LNET_KCORE_TMS_MAGIC = 0x33a1faabac111177,

	/* nlx_kcore_buffer::kb_magic (dissociablee) */
	M0_NET_LNET_KCORE_BUF_MAGIC = 0x33d1550c1ab1ee77,

	/* nlx_kcore_buffer_event::bev_magic (salsa bacilli) */
	M0_NET_LNET_KCORE_BEV_MAGIC = 0x335a15abac111177,

	/* drv_tms_tl::td_head_magic (cocci bacilli) */
	M0_NET_LNET_DEV_TMS_MAGIC   = 0x33c0cc1bac111177,

	/* drv_bufs_tl::td_head_magic (cicadellidae) */
	M0_NET_LNET_DEV_BUFS_MAGIC  = 0x33c1cade111dae77,

	/* drv_bevs_tl::td_head_magic (le cisco disco) */
	M0_NET_LNET_DEV_BEVS_MAGIC  = 0x331ec15c0d15c077,

	/* nlx_ucore_domain::ud_magic (blooded blade) */
	M0_NET_LNET_UCORE_DOM_MAGIC = 0x33b100dedb1ade77,

	/* nlx_ucore_transfer_mc::utm_magic (obsessed lila) */
	M0_NET_LNET_UCORE_TM_MAGIC  = 0x330b5e55ed111a77,

	/* nlx_ucore_buffer::ub_magic (ideal icefall) */
	M0_NET_LNET_UCORE_BUF_MAGIC = 0x331dea11cefa1177,

	/* nlx_core_buffer::cb_magic (edible icicle) */
	M0_NET_LNET_CORE_BUF_MAGIC = 0x33ed1b1e1c1c1e77,

	/* nlx_core_transfer_mc::ctm_magic (focal edifice) */
	M0_NET_LNET_CORE_TM_MAGIC  = 0x33f0ca1ed1f1ce77,

	/* nlx_xo_ep::xe_magic (failed fiasco) */
	M0_NET_LNET_XE_MAGIC = 0x33fa11edf1a5c077,

	/* bsb_tl::tl_head_magic (collides lail) */
	M0_NET_TEST_BSB_HEAD_MAGIC = 0x33c0111de51a1177,

	/* buf_status_bulk::bsb_magic (colloidal dal) */
	M0_NET_TEST_BSB_MAGIC = 0x33c01101da1da177,

	/* bsp_tl::tl_head_magic (delocalize so) */
	M0_NET_TEST_BSP_HEAD_MAGIC = 0x33de10ca112e5077,

	/* buf_status_ping::bsp_magic (lossless bafo) */
	M0_NET_TEST_BSP_MAGIC = 0x3310551e55baf077,

	/* buf_state_tl::tl_head_magic (official oecd) */
	M0_NET_TEST_BS_HEAD_MAGIC = 0x330ff1c1a10ecd77,

	/* buf_state::bs_link_magic (decibel aedes) */
	M0_NET_TEST_BS_LINK_MAGIC = 0x33dec1be1aede577,

	/* net_test_network_bds_header::ntnbh_magic (boldfaces esd) */
	M0_NET_TEST_NETWORK_BDS_MAGIC = 0x33b01dface5e5d77,

	/* net_test_network_bd::ntnbd_magic (socialized io) */
	M0_NET_TEST_NETWORK_BD_MAGIC = 0x3350c1a112ed1077,

	/* slist_params::sp_magic (sodaless adze) */
	M0_NET_TEST_SLIST_MAGIC = 0x3350da1e55ad2e77,

	/* ssb_tl::tl_head_magic (coloss caball) */
	M0_NET_TEST_SSB_HEAD_MAGIC = 0x33c01055caba1177,

	/* server_status_bulk::ssb_magic (closes doddie) */
	M0_NET_TEST_SSB_MAGIC = 0x33c105e5d0dd1e77,

	/* net_test_str_len::ntsl_magic (boldfaced sao) */
	M0_NET_TEST_STR_MAGIC = 0x33b01dfaced5a077,

	/* m0_net_test_timestamp::ntt_magic (allied cabiai) */
	M0_NET_TEST_TIMESTAMP_MAGIC = 0x33a111edcab1a177,

/* Pool Machine */
	/* m0_pool_event_link::pel_magic (pool evnt list)*/
	M0_POOL_EVENTS_LIST_MAGIC = 0x3360013747712777,

	/* poolmach_tl::tl_head_magic (pool evnt head)*/
	M0_POOL_EVENTS_HEAD_MAGIC = 0x33600137474ead77,


/* Request handler */
	/* m0_reqh_service::rs_magix (bacilli babel) */
	M0_REQH_SVC_MAGIC = 0x33bac1111babe177,

	/* m0_reqh_service_type::rst_magix (fiddless cobe) */
	M0_REQH_SVC_TYPE_MAGIC = 0x33f1dd1e55c0be77,

	/* m0_reqh_svc_tl::td_head_magic (calcific boss) */
	M0_REQH_SVC_HEAD_MAGIC = 0x33ca1c1f1cb05577,

	/* m0_reqh_rpc_mach_tl::td_head_magic (laissez eifel) */
	M0_REQH_RPC_MACH_HEAD_MAGIC = 0x331a155e2e1fe177,

	/* rev_conn_tl::rcf_link (abless ablaze) */
	M0_RM_REV_CONN_LIST_MAGIC = 0x33AB1E55AB1A2E77,

	/* rev_conn_tl::td_head_magic (belief abelia) */
	M0_RM_REV_CONN_LIST_HEAD_MAGIC = 0x33BE11EFABE11A77,

/* State Machine */
	/* m0_sm_conf::scf_magic (falsie zodiac) */
	M0_SM_CONF_MAGIC = 0x33FA151E20D1AC77,

/* Resource Manager */
	/* m0_rm_pin::rp_magix (bellicose bel) */
	M0_RM_PIN_MAGIC = 0x33be111c05ebe177,

	/* m0_rm_loan::rl_magix (biblical bill) */
	M0_RM_LOAN_MAGIC = 0x33b1b11ca1b11177,

	/* m0_rm_incoming::rin_magix (cacalia boole) */
	M0_RM_INCOMING_MAGIC = 0x33caca11ab001e77,

	/* m0_rm_outgoing::rog_magix (calcific call) */
	M0_RM_OUTGOING_MAGIC = 0x33ca1c1f1cca1177,

	/* pr_tl::td_head_magic (collide colsa) */
	M0_RM_CREDIT_PIN_HEAD_MAGIC = 0x33c0111dec015a77,

	/* pi_tl::td_head_magic (diabolise del) */
	M0_RM_INCOMING_PIN_HEAD_MAGIC = 0x33d1ab0115ede177,

	/* m0_rm_resource::r_magix (di doliolidae) */
	M0_RM_RESOURCE_MAGIC = 0x33d1d011011dae77,

	/* res_tl::td_head_magic (feeble eagles) */
	M0_RM_RESOURCE_HEAD_MAGIC = 0x33feeb1eea91e577,

	/* m0_rm_right::ri_magix (fizzle fields) */
	M0_RM_CREDIT_MAGIC = 0x33f1221ef1e1d577,

	/* m0_rm_ur_tl::td_head_magic (idolise iliad) */
	M0_RM_USAGE_CREDIT_HEAD_MAGIC = 0x331d0115e111ad77,

	/* remotes_tl::td_head_magic (offal oldfool) */
	M0_RM_REMOTE_OWNER_HEAD_MAGIC = 0x330ffa101df00177,

	/* m0_rm_remote::rem_magix (hobo hillbill) */
	M0_RM_REMOTE_MAGIC = 0x3309047714771977,

	/* m0_reqh_rm_service::rms_magix (seidel afield) */
	M0_RM_SERVICE_MAGIC = 0x335e1de1af1e1d77,

	/* rmsvc_owner_tl::ro_owner_linkage (eiffel doodle) */
	M0_RM_OWNER_LIST_MAGIC = 0x33E1FFE1D00D1E77,

	/* rmsvc_owner_tl::td_head_magic (scalic seabed) */
	M0_RM_OWNER_LIST_HEAD_MAGIC = 0x335CA11C5EABED77,

/* RPC */
	/* m0_rpc_service_type::svt_magix (seedless seel) */
	M0_RPC_SERVICE_TYPE_MAGIC = 0x335eed1e555ee177,

	/* m0_rpc_service::svc_magix (selfless self) */
	M0_RPC_SERVICE_MAGIC = 0x335e1f1e555e1f77,

	/* m0_rpc_services_tl::td_head_magic (lillie lisboa) */
	M0_RPC_SERVICES_HEAD_MAGIC = 0x3311111e115b0a77,

	/* rpc_service_types_tl::td_head_magic (fosilised foe) */
	M0_RPC_SERVICE_TYPES_HEAD_MAGIC = 0x33f055115edf0e77,

	/* m0_rpc_bulk_buf::bb_link (lidded liliac) */
	M0_RPC_BULK_BUF_MAGIC = 0x3311dded1111ac77,

	/* m0_rpc_bulk::rb_magic (leafless idol) */
	M0_RPC_BULK_MAGIC = 0x331eaf1e551d0177,

	/* m0_rpc_frm::f_magic (adelice dobie) */
	M0_RPC_FRM_MAGIC = 0x33ade11ced0b1e77,

	/* itemq_tl::td_head_magic (dazzled cliff) */
	M0_RPC_ITEMQ_HEAD_MAGIC = 0x33da221edc11ff77,

	/* m0_rpc_item::ri_magic (boiled coolie) */
	M0_RPC_ITEM_MAGIC = 0x33b011edc0011e77,

	/* rpcitem_tl::td_head_magic (disabled disc) */
	M0_RPC_ITEM_HEAD_MAGIC = 0x33d15ab1edd15c77,

	/* m0_rpc_item_source::ri_magic (ACCESSIBLE AC) */
	M0_RPC_ITEM_SOURCE_MAGIC = 0x33ACCE551B1EAC77,

	/* item_source_tl::td_head_magic (AC ACCESSIBLE) */
	M0_RPC_ITEM_SOURCE_HEAD_MAGIC = 0x33ACACCE551B1E77,

	/* rpc_buffer::rb_magic (iodized isaac) */
	M0_RPC_BUF_MAGIC = 0x3310d12ed15aac77,

	/* m0_rpc_machine::rm_magix (deboise aloof) */
	M0_RPC_MACHINE_MAGIC = 0x33deb015ea100f77,

	/* m0_rpc_item_type::rit_magic (daffodil dace) */
	M0_RPC_ITEM_TYPE_MAGIC = 0x33daff0d11dace77,

	/* rit_tl::td_head_magic (caboodle cold) */
	M0_RPC_ITEM_TYPE_HEAD_MAGIC = 0x33cab00d1ec01d77,

	/* packet_item_tl::td_head_magic (falloff eagle) */
	M0_RPC_PACKET_HEAD_MAGIC = 0x33fa110ffea91e77,

	/* m0_rpc_conn::c_magic (classic alibi) */
	M0_RPC_CONN_MAGIC = 0x33c1a551ca11b177,

	/* rpc_conn_tl::td_head_magic (bloodless god) */
	M0_RPC_CONN_HEAD_MAGIC = 0x33b100d1e5590d77,

	/* m0_rpc_session::s_magic (azido ballade) */
	M0_RPC_SESSION_MAGIC = 0x33a21d0ba11ade77,

	/* session_tl::td_head_magic (sizeable bell) */
	M0_RPC_SESSION_HEAD_MAGIC = 0x33512eb1ebe1177,

	/* m0_rpc_slot::sl_magic (delible diode) */
	M0_RPC_SLOT_MAGIC = 0x33de11b1ed10de77,

	/* ready_slot_tl::td_head_magic (assoil azzola) */
	M0_RPC_SLOT_HEAD_MAGIC = 0x33a55011a2201a77,

	/* slot_item_tl::td_head_magic (efface eiffel) */
	M0_RPC_SLOT_REF_HEAD_MAGIC = 0x33effacee1ffe177,

	/* m0_rpc_chan::rc_magic (faceless idol) */
	M0_RPC_CHAN_MAGIC = 0x33face1e551d0177,

	/* rpc_chans_tl::td_head_magic (idesia fossil) */
	M0_RPC_CHAN_HEAD_MAGIC = 0x331de51af0551177,

	/* m0_rpc_chan_watch::mw_magic "accessboiled*/
	M0_RPC_MACHINE_WATCH_MAGIC = 0x33ACCE55B011ED77,

	/* rmach_watch_tl::td_head_magic "COCOAA CALLED" */
	M0_RPC_MACHINE_WATCH_HEAD_MAGIC = 0x33C0C0AACA11ED77,

/* stob */
	/* m0_stob_cacheable::ca_magix (bilobed flood) */
	M0_STOB_CACHEABLE_MAGIX = 0x33b110bedf100d77,

	/* stob/cache.c:cache_tl::td_head_magic (faded ballade) */
	M0_STOB_CACHE_MAGIX     = 0x33FADEDBA11ADE77,

/* Trace */
	/* m0_trace_rec_header::trh_magic (foldable doll) */
	M0_TRACE_MAGIC = 0x33f01dab1ed01177,

	/* m0_trace_descr::td_magic (badass coders) */
	M0_TRACE_DESCR_MAGIC = 0x33bada55c0de2577,

/* BE */
	/* m0_be_tx::t_magic (I feel good) */
	M0_BE_TX_MAGIC = 0x331fee190000d177,

	/* m0_be_tx_engine::te_txs[] (lifeless gel)  */
	M0_BE_TX_ENGINE_MAGIC = 0x3311fe1e556e1277,

	/* m0_be_tx_group::tg_txs (codified bee)  */
	M0_BE_TX_GROUP_MAGIC = 0x33c0d1f1edbee377,

/* lib */
	/* hashlist::hl_magic = invincibilis */
	M0_LIB_HASHLIST_MAGIC = 0x3319519c1b111577,

};

#endif /* __MERO_MERO_MAGIC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
