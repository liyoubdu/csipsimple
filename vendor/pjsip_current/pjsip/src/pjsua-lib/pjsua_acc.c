/* $Id: pjsua_acc.c 3139 2010-04-14 08:12:08Z nanang $ */
/* 
 * Copyright (C) 2008-2009 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>


#define THIS_FILE		"pjsua_acc.c"


static void schedule_reregistration(pjsua_acc *acc);

/*
 * Get number of current accounts.
 */
PJ_DEF(unsigned) pjsua_acc_get_count(void)
{
    return pjsua_var.acc_cnt;
}


/*
 * Check if the specified account ID is valid.
 */
PJ_DEF(pj_bool_t) pjsua_acc_is_valid(pjsua_acc_id acc_id)
{
    return acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc) &&
	   pjsua_var.acc[acc_id].valid;
}


/*
 * Set default account
 */
PJ_DEF(pj_status_t) pjsua_acc_set_default(pjsua_acc_id acc_id)
{
    pjsua_var.default_acc = acc_id;
    return PJ_SUCCESS;
}


/*
 * Get default account.
 */
PJ_DEF(pjsua_acc_id) pjsua_acc_get_default(void)
{
    return pjsua_var.default_acc;
}


/*
 * Copy account configuration.
 */
PJ_DEF(void) pjsua_acc_config_dup( pj_pool_t *pool,
				   pjsua_acc_config *dst,
				   const pjsua_acc_config *src)
{
    unsigned i;

    pj_memcpy(dst, src, sizeof(pjsua_acc_config));

    pj_strdup_with_null(pool, &dst->id, &src->id);
    pj_strdup_with_null(pool, &dst->reg_uri, &src->reg_uri);
    pj_strdup_with_null(pool, &dst->force_contact, &src->force_contact);
    pj_strdup_with_null(pool, &dst->pidf_tuple_id, &src->pidf_tuple_id);

    dst->proxy_cnt = src->proxy_cnt;
    for (i=0; i<src->proxy_cnt; ++i)
	pj_strdup_with_null(pool, &dst->proxy[i], &src->proxy[i]);

    dst->reg_timeout = src->reg_timeout;
    dst->cred_count = src->cred_count;

    for (i=0; i<src->cred_count; ++i) {
	pjsip_cred_dup(pool, &dst->cred_info[i], &src->cred_info[i]);
    }

    dst->ka_interval = src->ka_interval;
    pj_strdup(pool, &dst->ka_data, &src->ka_data);
}


/*
 * Initialize a new account (after configuration is set).
 */
static pj_status_t initialize_acc(unsigned acc_id)
{
    pjsua_acc_config *acc_cfg = &pjsua_var.acc[acc_id].cfg;
    pjsua_acc *acc = &pjsua_var.acc[acc_id];
    pjsip_name_addr *name_addr;
    pjsip_sip_uri *sip_uri, *sip_reg_uri;
    pj_status_t status;
    unsigned i;

    /* Need to parse local_uri to get the elements: */

    name_addr = (pjsip_name_addr*)
		    pjsip_parse_uri(acc->pool, acc_cfg->id.ptr,
				    acc_cfg->id.slen, 
				    PJSIP_PARSE_URI_AS_NAMEADDR);
    if (name_addr == NULL) {
	pjsua_perror(THIS_FILE, "Invalid local URI", 
		     PJSIP_EINVALIDURI);
	return PJSIP_EINVALIDURI;
    }

    /* Local URI MUST be a SIP or SIPS: */

    if (!PJSIP_URI_SCHEME_IS_SIP(name_addr) && 
	!PJSIP_URI_SCHEME_IS_SIPS(name_addr)) 
    {
	pjsua_perror(THIS_FILE, "Invalid local URI", 
		     PJSIP_EINVALIDSCHEME);
	return PJSIP_EINVALIDSCHEME;
    }


    /* Get the SIP URI object: */
    sip_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(name_addr);


    /* Parse registrar URI, if any */
    if (acc_cfg->reg_uri.slen) {
	pjsip_uri *reg_uri;

	reg_uri = pjsip_parse_uri(acc->pool, acc_cfg->reg_uri.ptr,
				  acc_cfg->reg_uri.slen, 0);
	if (reg_uri == NULL) {
	    pjsua_perror(THIS_FILE, "Invalid registrar URI", 
			 PJSIP_EINVALIDURI);
	    return PJSIP_EINVALIDURI;
	}

	/* Registrar URI MUST be a SIP or SIPS: */
	if (!PJSIP_URI_SCHEME_IS_SIP(reg_uri) && 
	    !PJSIP_URI_SCHEME_IS_SIPS(reg_uri)) 
	{
	    pjsua_perror(THIS_FILE, "Invalid registar URI", 
			 PJSIP_EINVALIDSCHEME);
	    return PJSIP_EINVALIDSCHEME;
	}

	sip_reg_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(reg_uri);

    } else {
	sip_reg_uri = NULL;
    }

    /* Save the user and domain part. These will be used when finding an 
     * account for incoming requests.
     */
    acc->display = name_addr->display;
    acc->user_part = sip_uri->user;
    acc->srv_domain = sip_uri->host;
    acc->srv_port = 0;

    if (sip_reg_uri) {
	acc->srv_port = sip_reg_uri->port;
    }

    /* Create Contact header if not present. */
    //if (acc_cfg->contact.slen == 0) {
    //	acc_cfg->contact = acc_cfg->id;
    //}

    /* Build account route-set from outbound proxies and route set from 
     * account configuration.
     */
    pj_list_init(&acc->route_set);

    for (i=0; i<pjsua_var.ua_cfg.outbound_proxy_cnt; ++i) {
    	pj_str_t hname = { "Route", 5};
	pjsip_route_hdr *r;
	pj_str_t tmp;

	pj_strdup_with_null(acc->pool, &tmp, 
			    &pjsua_var.ua_cfg.outbound_proxy[i]);
	r = (pjsip_route_hdr*)
	    pjsip_parse_hdr(acc->pool, &hname, tmp.ptr, tmp.slen, NULL);
	if (r == NULL) {
	    pjsua_perror(THIS_FILE, "Invalid outbound proxy URI",
			 PJSIP_EINVALIDURI);
	    return PJSIP_EINVALIDURI;
	}
	pj_list_push_back(&acc->route_set, r);
    }

    for (i=0; i<acc_cfg->proxy_cnt; ++i) {
    	pj_str_t hname = { "Route", 5};
	pjsip_route_hdr *r;
	pj_str_t tmp;

	pj_strdup_with_null(acc->pool, &tmp, &acc_cfg->proxy[i]);
	r = (pjsip_route_hdr*)
	    pjsip_parse_hdr(acc->pool, &hname, tmp.ptr, tmp.slen, NULL);
	if (r == NULL) {
	    pjsua_perror(THIS_FILE, "Invalid URI in account route set",
			 PJ_EINVAL);
	    return PJ_EINVAL;
	}
	pj_list_push_back(&acc->route_set, r);
    }

    
    /* Concatenate credentials from account config and global config */
    acc->cred_cnt = 0;
    for (i=0; i<acc_cfg->cred_count; ++i) {
	acc->cred[acc->cred_cnt++] = acc_cfg->cred_info[i];
    }
    for (i=0; i<pjsua_var.ua_cfg.cred_count && 
	      acc->cred_cnt < PJ_ARRAY_SIZE(acc->cred); ++i)
    {
	acc->cred[acc->cred_cnt++] = pjsua_var.ua_cfg.cred_info[i];
    }

    status = pjsua_pres_init_acc(acc_id);
    if (status != PJ_SUCCESS)
	return status;

    /* Mark account as valid */
    pjsua_var.acc[acc_id].valid = PJ_TRUE;

    /* Insert account ID into account ID array, sorted by priority */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	if ( pjsua_var.acc[pjsua_var.acc_ids[i]].cfg.priority <
	     pjsua_var.acc[acc_id].cfg.priority)
	{
	    break;
	}
    }
    pj_array_insert(pjsua_var.acc_ids, sizeof(pjsua_var.acc_ids[0]),
		    pjsua_var.acc_cnt, i, &acc_id);

    return PJ_SUCCESS;
}


/*
 * Add a new account to pjsua.
 */
PJ_DEF(pj_status_t) pjsua_acc_add( const pjsua_acc_config *cfg,
				   pj_bool_t is_default,
				   pjsua_acc_id *p_acc_id)
{
    pjsua_acc *acc;
    unsigned i, id;
    pj_status_t status;

    PJ_ASSERT_RETURN(pjsua_var.acc_cnt < PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_ETOOMANY);

    /* Must have a transport */
    PJ_ASSERT_RETURN(pjsua_var.tpdata[0].data.ptr != NULL, PJ_EINVALIDOP);

    PJSUA_LOCK();

    /* Find empty account id. */
    for (id=0; id < PJ_ARRAY_SIZE(pjsua_var.acc); ++id) {
	if (pjsua_var.acc[id].valid == PJ_FALSE)
	    break;
    }

    /* Expect to find a slot */
    PJ_ASSERT_ON_FAIL(	id < PJ_ARRAY_SIZE(pjsua_var.acc), 
			{PJSUA_UNLOCK(); return PJ_EBUG;});

    acc = &pjsua_var.acc[id];

    /* Create pool for this account. */
    if (acc->pool)
	pj_pool_reset(acc->pool);
    else
	acc->pool = pjsua_pool_create("acc%p", 512, 256);

    /* Copy config */
    pjsua_acc_config_dup(acc->pool, &pjsua_var.acc[id].cfg, cfg);
    
    /* Normalize registration timeout */
    if (pjsua_var.acc[id].cfg.reg_uri.slen &&
	pjsua_var.acc[id].cfg.reg_timeout == 0)
    {
	pjsua_var.acc[id].cfg.reg_timeout = PJSUA_REG_INTERVAL;
    }

    /* Check the route URI's and force loose route if required */
    for (i=0; i<acc->cfg.proxy_cnt; ++i) {
	status = normalize_route_uri(acc->pool, &acc->cfg.proxy[i]);
	if (status != PJ_SUCCESS)
	    return status;
    }

    status = initialize_acc(id);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error adding account", status);
	pj_pool_release(acc->pool);
	acc->pool = NULL;
	PJSUA_UNLOCK();
	return status;
    }

    if (is_default)
	pjsua_var.default_acc = id;

    if (p_acc_id)
	*p_acc_id = id;

    pjsua_var.acc_cnt++;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Account %.*s added with id %d",
	      (int)cfg->id.slen, cfg->id.ptr, id));

    /* If accounts has registration enabled, start registration */
    if (pjsua_var.acc[id].cfg.reg_uri.slen)
	pjsua_acc_set_registration(id, PJ_TRUE);
    else {
	/* Otherwise subscribe to MWI, if it's enabled */
	if (pjsua_var.acc[id].cfg.mwi_enabled)
	    pjsua_start_mwi(&pjsua_var.acc[id]);
    }

    return PJ_SUCCESS;
}


/*
 * Add local account
 */
PJ_DEF(pj_status_t) pjsua_acc_add_local( pjsua_transport_id tid,
					 pj_bool_t is_default,
					 pjsua_acc_id *p_acc_id)
{
    pjsua_acc_config cfg;
    pjsua_transport_data *t = &pjsua_var.tpdata[tid];
    const char *beginquote, *endquote;
    char transport_param[32];
    char uri[PJSIP_MAX_URL_SIZE];

    /* ID must be valid */
    PJ_ASSERT_RETURN(tid>=0 && tid<(int)PJ_ARRAY_SIZE(pjsua_var.tpdata), 
		     PJ_EINVAL);

    /* Transport must be valid */
    PJ_ASSERT_RETURN(t->data.ptr != NULL, PJ_EINVAL);
    
    pjsua_acc_config_default(&cfg);

    /* Lower the priority of local account */
    --cfg.priority;

    /* Enclose IPv6 address in square brackets */
    if (t->type & PJSIP_TRANSPORT_IPV6) {
	beginquote = "[";
	endquote = "]";
    } else {
	beginquote = endquote = "";
    }

    /* Don't add transport parameter if it's UDP */
    if (t->type!=PJSIP_TRANSPORT_UDP && t->type!=PJSIP_TRANSPORT_UDP6) {
	pj_ansi_snprintf(transport_param, sizeof(transport_param),
		         ";transport=%s",
			 pjsip_transport_get_type_name(t->type));
    } else {
	transport_param[0] = '\0';
    }

    /* Build URI for the account */
    pj_ansi_snprintf(uri, PJSIP_MAX_URL_SIZE,
		     "<sip:%s%.*s%s:%d%s>", 
		     beginquote,
		     (int)t->local_name.host.slen,
		     t->local_name.host.ptr,
		     endquote,
		     t->local_name.port,
		     transport_param);

    cfg.id = pj_str(uri);
    
    return pjsua_acc_add(&cfg, is_default, p_acc_id);
}


/*
 * Set arbitrary data to be associated with the account.
 */
PJ_DEF(pj_status_t) pjsua_acc_set_user_data(pjsua_acc_id acc_id,
					    void *user_data)
{
    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();

    pjsua_var.acc[acc_id].cfg.user_data = user_data;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*
 * Retrieve arbitrary data associated with the account.
 */
PJ_DEF(void*) pjsua_acc_get_user_data(pjsua_acc_id acc_id)
{
    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     NULL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, NULL);

    return pjsua_var.acc[acc_id].cfg.user_data;
}


/*
 * Delete account.
 */
PJ_DEF(pj_status_t) pjsua_acc_del(pjsua_acc_id acc_id)
{
    unsigned i;

    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();

    /* Cancel any re-registration timer */
    pjsua_cancel_timer(&pjsua_var.acc[acc_id].auto_rereg.timer);

    /* Delete registration */
    if (pjsua_var.acc[acc_id].regc != NULL) {
	pjsua_acc_set_registration(acc_id, PJ_FALSE);
	if (pjsua_var.acc[acc_id].regc) {
	    pjsip_regc_destroy(pjsua_var.acc[acc_id].regc);
	}
	pjsua_var.acc[acc_id].regc = NULL;
    }

    /* Delete server presence subscription */
    pjsua_pres_delete_acc(acc_id);

    /* Release account pool */
    if (pjsua_var.acc[acc_id].pool) {
	pj_pool_release(pjsua_var.acc[acc_id].pool);
	pjsua_var.acc[acc_id].pool = NULL;
    }

    /* Invalidate */
    pjsua_var.acc[acc_id].valid = PJ_FALSE;
    pjsua_var.acc[acc_id].contact.slen = 0;

    /* Remove from array */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	if (pjsua_var.acc_ids[i] == acc_id)
	    break;
    }
    if (i != pjsua_var.acc_cnt) {
	pj_array_erase(pjsua_var.acc_ids, sizeof(pjsua_var.acc_ids[0]),
		       pjsua_var.acc_cnt, i);
	--pjsua_var.acc_cnt;
    }

    /* Leave the calls intact, as I don't think calls need to
     * access account once it's created
     */

    /* Update default account */
    if (pjsua_var.default_acc == acc_id)
	pjsua_var.default_acc = 0;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Account id %d deleted", acc_id));

    return PJ_SUCCESS;
}


/*
 * Modify account information.
 */
PJ_DEF(pj_status_t) pjsua_acc_modify( pjsua_acc_id acc_id,
				      const pjsua_acc_config *cfg)
{
    PJ_TODO(pjsua_acc_modify);
    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(cfg);
    return PJ_EINVALIDOP;
}


/*
 * Modify account's presence status to be advertised to remote/presence
 * subscribers.
 */
PJ_DEF(pj_status_t) pjsua_acc_set_online_status( pjsua_acc_id acc_id,
						 pj_bool_t is_online)
{
    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    pjsua_var.acc[acc_id].online_status = is_online;
    pj_bzero(&pjsua_var.acc[acc_id].rpid, sizeof(pjrpid_element));
    pjsua_pres_update_acc(acc_id, PJ_FALSE);
    return PJ_SUCCESS;
}


/* 
 * Set online status with extended information 
 */
PJ_DEF(pj_status_t) pjsua_acc_set_online_status2( pjsua_acc_id acc_id,
						  pj_bool_t is_online,
						  const pjrpid_element *pr)
{
    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();
    pjsua_var.acc[acc_id].online_status = is_online;
    pjrpid_element_dup(pjsua_var.acc[acc_id].pool, &pjsua_var.acc[acc_id].rpid, pr);
    PJSUA_UNLOCK();

    pjsua_pres_update_acc(acc_id, PJ_TRUE);
    return PJ_SUCCESS;
}

/* Check if IP is private IP address */
static pj_bool_t is_private_ip(const pj_str_t *addr)
{
    const pj_str_t private_net[] = 
    {
	{ "10.", 3 },
	{ "127.", 4 },
	{ "172.16.", 7 },
	{ "192.168.", 8 }
    };
    unsigned i;

    for (i=0; i<PJ_ARRAY_SIZE(private_net); ++i) {
	if (pj_strncmp(addr, &private_net[i], private_net[i].slen)==0)
	    return PJ_TRUE;
    }

    return PJ_FALSE;
}

/* Update NAT address from the REGISTER response */
static pj_bool_t acc_check_nat_addr(pjsua_acc *acc,
				    struct pjsip_regc_cbparam *param)
{
    pjsip_transport *tp;
    const pj_str_t *via_addr;
    pj_pool_t *pool;
    int rport;
    pjsip_sip_uri *uri;
    pjsip_via_hdr *via;
    pj_sockaddr contact_addr;
    pj_sockaddr recv_addr;
    pj_status_t status;
    pj_bool_t matched;
    pj_str_t srv_ip;
    pjsip_contact_hdr *contact_hdr;
    const pj_str_t STR_CONTACT = { "Contact", 7 };

    tp = param->rdata->tp_info.transport;

    /* Only update if account is configured to auto-update */
    if (acc->cfg.allow_contact_rewrite == PJ_FALSE)
	return PJ_FALSE;

#if 0
    // Always update
    // See http://lists.pjsip.org/pipermail/pjsip_lists.pjsip.org/2008-March/002178.html

    /* For UDP, only update if STUN is enabled (for now).
     * For TCP/TLS, always check.
     */
    if ((tp->key.type == PJSIP_TRANSPORT_UDP &&
	 (pjsua_var.ua_cfg.stun_domain.slen != 0 ||
	 (pjsua_var.ua_cfg.stun_host.slen != 0))  ||
	(tp->key.type == PJSIP_TRANSPORT_TCP) ||
	(tp->key.type == PJSIP_TRANSPORT_TLS))
    {
	/* Yes we will check */
    } else {
	return PJ_FALSE;
    }
#endif

    /* Get the received and rport info */
    via = param->rdata->msg_info.via;
    if (via->rport_param < 1) {
	/* Remote doesn't support rport */
	rport = via->sent_by.port;
	if (rport==0) {
	    pjsip_transport_type_e tp_type;
	    tp_type = (pjsip_transport_type_e) tp->key.type;
	    rport = pjsip_transport_get_default_port_for_type(tp_type);
	}
    } else
	rport = via->rport_param;

    if (via->recvd_param.slen != 0)
	via_addr = &via->recvd_param;
    else
	via_addr = &via->sent_by.host;

    /* Compare received and rport with the URI in our registration */
    pool = pjsua_pool_create("tmp", 512, 512);
    contact_hdr = (pjsip_contact_hdr*)
		  pjsip_parse_hdr(pool, &STR_CONTACT, acc->contact.ptr, 
				  acc->contact.slen, NULL);
    pj_assert(contact_hdr != NULL);
    uri = (pjsip_sip_uri*) contact_hdr->uri;
    pj_assert(uri != NULL);
    uri = (pjsip_sip_uri*) pjsip_uri_get_uri(uri);

    if (uri->port == 0) {
	pjsip_transport_type_e tp_type;
	tp_type = (pjsip_transport_type_e) tp->key.type;
	uri->port = pjsip_transport_get_default_port_for_type(tp_type);
    }

    /* Convert IP address strings into sockaddr for comparison.
     * (http://trac.pjsip.org/repos/ticket/863)
     */
    status = pj_sockaddr_parse(pj_AF_UNSPEC(), 0, &uri->host, 
			       &contact_addr);
    if (status == PJ_SUCCESS)
	status = pj_sockaddr_parse(pj_AF_UNSPEC(), 0, via_addr, 
				   &recv_addr);
    if (status == PJ_SUCCESS) {
	/* Compare the addresses as sockaddr according to the ticket above */
	matched = (uri->port == rport &&
		   pj_sockaddr_cmp(&contact_addr, &recv_addr)==0);
    } else {
	/* Compare the addresses as string, as before */
	matched = (uri->port == rport &&
		   pj_stricmp(&uri->host, via_addr)==0);
    }

    if (matched) {
	/* Address doesn't change */
	pj_pool_release(pool);
	return PJ_FALSE;
    }

    /* Get server IP */
    srv_ip = pj_str(param->rdata->pkt_info.src_name);

    /* At this point we've detected that the address as seen by registrar.
     * has changed.
     */

    /* Do not switch if both Contact and server's IP address are
     * public but response contains private IP. A NAT in the middle
     * might have messed up with the SIP packets. See:
     * http://trac.pjsip.org/repos/ticket/643
     *
     * This exception can be disabled by setting allow_contact_rewrite
     * to 2. In this case, the switch will always be done whenever there
     * is difference in the IP address in the response.
     */
    if (acc->cfg.allow_contact_rewrite != 2 && !is_private_ip(&uri->host) &&
	!is_private_ip(&srv_ip) && is_private_ip(via_addr))
    {
	/* Don't switch */
	pj_pool_release(pool);
	return PJ_FALSE;
    }

    /* Also don't switch if only the port number part is different, and
     * the Via received address is private.
     * See http://trac.pjsip.org/repos/ticket/864
     */
    if (acc->cfg.allow_contact_rewrite != 2 &&
	pj_sockaddr_cmp(&contact_addr, &recv_addr)==0 &&
	is_private_ip(via_addr))
    {
	/* Don't switch */
	pj_pool_release(pool);
	return PJ_FALSE;
    }

    PJ_LOG(3,(THIS_FILE, "IP address change detected for account %d "
			 "(%.*s:%d --> %.*s:%d). Updating registration..",
			 acc->index,
			 (int)uri->host.slen,
			 uri->host.ptr,
			 uri->port,
			 (int)via_addr->slen,
			 via_addr->ptr,
			 rport));

    /* Unregister current contact */
    pjsua_acc_set_registration(acc->index, PJ_FALSE);
    if (acc->regc != NULL) {
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;
	acc->contact.slen = 0;
    }

    /* Update account's Contact header */
    {
	char *tmp;
	const char *beginquote, *endquote;
	int len;

	/* Enclose IPv6 address in square brackets */
	if (tp->key.type & PJSIP_TRANSPORT_IPV6) {
	    beginquote = "[";
	    endquote = "]";
	} else {
	    beginquote = endquote = "";
	}

	tmp = (char*) pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
	len = pj_ansi_snprintf(tmp, PJSIP_MAX_URL_SIZE,
			       "<sip:%.*s%s%s%.*s%s:%d;transport=%s%.*s>%.*s",
			       (int)acc->user_part.slen,
			       acc->user_part.ptr,
			       (acc->user_part.slen? "@" : ""),
			       beginquote,
			       (int)via_addr->slen,
			       via_addr->ptr,
			       endquote,
			       rport,
			       tp->type_name,
			       (int)acc->cfg.contact_uri_params.slen,
			       acc->cfg.contact_uri_params.ptr,
			       (int)acc->cfg.contact_params.slen,
			       acc->cfg.contact_params.ptr);
	if (len < 1) {
	    PJ_LOG(1,(THIS_FILE, "URI too long"));
	    pj_pool_release(pool);
	    return PJ_FALSE;
	}
	pj_strdup2_with_null(acc->pool, &acc->contact, tmp);
    }

    /* Always update, by http://trac.pjsip.org/repos/ticket/864. */
    pj_strdup_with_null(tp->pool, &tp->local_name.host, via_addr);
    tp->local_name.port = rport;

    /* Perform new registration */
    pjsua_acc_set_registration(acc->index, PJ_TRUE);

    pj_pool_release(pool);

    return PJ_TRUE;
}

/* Check and update Service-Route header */
void update_service_route(pjsua_acc *acc, pjsip_rx_data *rdata)
{
    pjsip_generic_string_hdr *hsr = NULL;
    pjsip_route_hdr *hr, *h;
    const pj_str_t HNAME = { "Service-Route", 13 };
    const pj_str_t HROUTE = { "Route", 5 };
    pjsip_uri *uri[PJSUA_ACC_MAX_PROXIES];
    unsigned i, uri_cnt = 0, rcnt;

    /* Find and parse Service-Route headers */
    for (;;) {
	char saved;
	int parsed_len;

	/* Find Service-Route header */
	hsr = (pjsip_generic_string_hdr*)
	      pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &HNAME, hsr);
	if (!hsr)
	    break;

	/* Parse as Route header since the syntax is similar. This may
	 * return more than one headers.
	 */
	saved = hsr->hvalue.ptr[hsr->hvalue.slen];
	hsr->hvalue.ptr[hsr->hvalue.slen] = '\0';
	hr = (pjsip_route_hdr*)
	     pjsip_parse_hdr(rdata->tp_info.pool, &HROUTE, hsr->hvalue.ptr,
			     hsr->hvalue.slen, &parsed_len);
	hsr->hvalue.ptr[hsr->hvalue.slen] = saved;

	if (hr == NULL) {
	    /* Error */
	    PJ_LOG(1,(THIS_FILE, "Error parsing Service-Route header"));
	    return;
	}

	/* Save each URI in the result */
	h = hr;
	do {
	    if (!PJSIP_URI_SCHEME_IS_SIP(h->name_addr.uri) &&
		!PJSIP_URI_SCHEME_IS_SIPS(h->name_addr.uri))
	    {
		PJ_LOG(1,(THIS_FILE,"Error: non SIP URI in Service-Route: %.*s",
			  (int)hsr->hvalue.slen, hsr->hvalue.ptr));
		return;
	    }

	    uri[uri_cnt++] = h->name_addr.uri;
	    h = h->next;
	} while (h != hr && uri_cnt != PJ_ARRAY_SIZE(uri));

	if (h != hr) {
	    PJ_LOG(1,(THIS_FILE, "Error: too many Service-Route headers"));
	    return;
	}

	/* Prepare to find next Service-Route header */
	hsr = hsr->next;
	if ((void*)hsr == (void*)&rdata->msg_info.msg->hdr)
	    break;
    }

    if (uri_cnt == 0)
	return;

    /* 
     * Update account's route set 
     */
    
    /* First remove all routes which are not the outbound proxies */
    rcnt = pj_list_size(&acc->route_set);
    if (rcnt != pjsua_var.ua_cfg.outbound_proxy_cnt + acc->cfg.proxy_cnt) {
	for (i=pjsua_var.ua_cfg.outbound_proxy_cnt + acc->cfg.proxy_cnt, 
		hr=acc->route_set.prev; 
	     i<rcnt; 
	     ++i)
	 {
	    pjsip_route_hdr *prev = hr->prev;
	    pj_list_erase(hr);
	    hr = prev;
	 }
    }

    /* Then append the Service-Route URIs */
    for (i=0; i<uri_cnt; ++i) {
	hr = pjsip_route_hdr_create(acc->pool);
	hr->name_addr.uri = (pjsip_uri*)pjsip_uri_clone(acc->pool, uri[i]);
	pj_list_push_back(&acc->route_set, hr);
    }

    /* Done */

    PJ_LOG(4,(THIS_FILE, "Service-Route updated for acc %d with %d URI(s)",
	      acc->index, uri_cnt));
}


/* Keep alive timer callback */
static void keep_alive_timer_cb(pj_timer_heap_t *th, pj_timer_entry *te)
{
    pjsua_acc *acc;
    pjsip_tpselector tp_sel;
    pj_time_val delay;
    char addrtxt[PJ_INET6_ADDRSTRLEN];
    pj_status_t status;

    PJ_UNUSED_ARG(th);

    PJSUA_LOCK();

    te->id = PJ_FALSE;

    acc = (pjsua_acc*) te->user_data;

    /* Select the transport to send the packet */
    pj_bzero(&tp_sel, sizeof(tp_sel));
    tp_sel.type = PJSIP_TPSELECTOR_TRANSPORT;
    tp_sel.u.transport = acc->ka_transport;

    PJ_LOG(5,(THIS_FILE, 
	      "Sending %d bytes keep-alive packet for acc %d to %s",
	      acc->cfg.ka_data.slen, acc->index,
	      pj_sockaddr_print(&acc->ka_target, addrtxt, sizeof(addrtxt),3)));

    /* Send raw packet */
    status = pjsip_tpmgr_send_raw(pjsip_endpt_get_tpmgr(pjsua_var.endpt),
				  PJSIP_TRANSPORT_UDP, &tp_sel,
				  NULL, acc->cfg.ka_data.ptr, 
				  acc->cfg.ka_data.slen, 
				  &acc->ka_target, acc->ka_target_len,
				  NULL, NULL);

    if (status != PJ_SUCCESS && status != PJ_EPENDING) {
	pjsua_perror(THIS_FILE, "Error sending keep-alive packet", status);
    }

    /* Reschedule next timer */
    delay.sec = acc->cfg.ka_interval;
    delay.msec = 0;
    status = pjsip_endpt_schedule_timer(pjsua_var.endpt, te, &delay);
    if (status == PJ_SUCCESS) {
	te->id = PJ_TRUE;
    } else {
	pjsua_perror(THIS_FILE, "Error starting keep-alive timer", status);
    }

    PJSUA_UNLOCK();
}


/* Update keep-alive for the account */
static void update_keep_alive(pjsua_acc *acc, pj_bool_t start,
			      struct pjsip_regc_cbparam *param)
{
    /* In all cases, stop keep-alive timer if it's running. */
    if (acc->ka_timer.id) {
	pjsip_endpt_cancel_timer(pjsua_var.endpt, &acc->ka_timer);
	acc->ka_timer.id = PJ_FALSE;

	pjsip_transport_dec_ref(acc->ka_transport);
	acc->ka_transport = NULL;
    }

    if (start) {
	pj_time_val delay;
	pj_status_t status;

	/* Only do keep-alive if:
	 *  - ka_interval is not zero in the account, and
	 *  - transport is UDP.
	 *
	 * Previously we only enabled keep-alive when STUN is enabled, since
	 * we thought that keep-alive is only needed in Internet situation.
	 * But it has been discovered that Windows Firewall on WinXP also
	 * needs to be kept-alive, otherwise incoming packets will be dropped.
	 * So because of this, now keep-alive is always enabled for UDP,
	 * regardless of whether STUN is enabled or not.
	 *
	 * Note that this applies only for UDP. For TCP/TLS, the keep-alive
	 * is done by the transport layer.
	 */
	if (/*pjsua_var.stun_srv.ipv4.sin_family == 0 ||*/
	    acc->cfg.ka_interval == 0 ||
	    param->rdata->tp_info.transport->key.type != PJSIP_TRANSPORT_UDP)
	{
	    /* Keep alive is not necessary */
	    return;
	}

	/* Save transport and destination address. */
	acc->ka_transport = param->rdata->tp_info.transport;
	pjsip_transport_add_ref(acc->ka_transport);
	pj_memcpy(&acc->ka_target, &param->rdata->pkt_info.src_addr,
		  param->rdata->pkt_info.src_addr_len);
	acc->ka_target_len = param->rdata->pkt_info.src_addr_len;

	/* Setup and start the timer */
	acc->ka_timer.cb = &keep_alive_timer_cb;
	acc->ka_timer.user_data = (void*)acc;

	delay.sec = acc->cfg.ka_interval;
	delay.msec = 0;
	status = pjsip_endpt_schedule_timer(pjsua_var.endpt, &acc->ka_timer, 
					    &delay);
	if (status == PJ_SUCCESS) {
	    acc->ka_timer.id = PJ_TRUE;
	    PJ_LOG(4,(THIS_FILE, "Keep-alive timer started for acc %d, "
				 "destination:%s:%d, interval:%ds",
				 acc->index,
				 param->rdata->pkt_info.src_name,
				 param->rdata->pkt_info.src_port,
				 acc->cfg.ka_interval));
	} else {
	    acc->ka_timer.id = PJ_FALSE;
	    pjsip_transport_dec_ref(acc->ka_transport);
	    acc->ka_transport = NULL;
	    pjsua_perror(THIS_FILE, "Error starting keep-alive timer", status);
	}
    }
}


/*
 * This callback is called by pjsip_regc when outgoing register
 * request has completed.
 */
static void regc_cb(struct pjsip_regc_cbparam *param)
{

    pjsua_acc *acc = (pjsua_acc*) param->token;

    if (param->regc != acc->regc)
	return;

    PJSUA_LOCK();

    /*
     * Print registration status.
     */
    if (param->status!=PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "SIP registration error", 
		     param->status);
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;
	acc->contact.slen = 0;
	
	/* Stop keep-alive timer if any. */
	update_keep_alive(acc, PJ_FALSE, NULL);

    } else if (param->code < 0 || param->code >= 300) {
	PJ_LOG(2, (THIS_FILE, "SIP registration failed, status=%d (%.*s)", 
		   param->code, 
		   (int)param->reason.slen, param->reason.ptr));
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;
	acc->contact.slen = 0;

	/* Stop keep-alive timer if any. */
	update_keep_alive(acc, PJ_FALSE, NULL);

    } else if (PJSIP_IS_STATUS_IN_CLASS(param->code, 200)) {

	/* Update auto registration flag */
	acc->auto_rereg.active = PJ_FALSE;
	acc->auto_rereg.attempt_cnt = 0;

	if (param->expiration < 1) {
	    pjsip_regc_destroy(acc->regc);
	    acc->regc = NULL;
	    acc->contact.slen = 0;

	    /* Stop keep-alive timer if any. */
	    update_keep_alive(acc, PJ_FALSE, NULL);

	    PJ_LOG(3,(THIS_FILE, "%s: unregistration success",
		      pjsua_var.acc[acc->index].cfg.id.ptr));
	} else {
	    /* Check NAT bound address */
	    if (acc_check_nat_addr(acc, param)) {
		/* Update address, don't notify application yet */
		PJSUA_UNLOCK();
		return;
	    }

	    /* Check and update Service-Route header */
	    update_service_route(acc, param->rdata);

	    PJ_LOG(3, (THIS_FILE, 
		       "%s: registration success, status=%d (%.*s), "
		       "will re-register in %d seconds", 
		       pjsua_var.acc[acc->index].cfg.id.ptr,
		       param->code,
		       (int)param->reason.slen, param->reason.ptr,
		       param->expiration));

	    /* Start keep-alive timer if necessary. */
	    update_keep_alive(acc, PJ_TRUE, param);

	    /* Send initial PUBLISH if it is enabled */
	    if (acc->cfg.publish_enabled && acc->publish_sess==NULL)
		pjsua_pres_init_publish_acc(acc->index);

	    /* Subscribe to MWI, if it's enabled */
	    if (acc->cfg.mwi_enabled)
		pjsua_start_mwi(acc);
	}

    } else {
	PJ_LOG(4, (THIS_FILE, "SIP registration updated status=%d", param->code));
    }

    acc->reg_last_err = param->status;
    acc->reg_last_code = param->code;

    if (pjsua_var.ua_cfg.cb.on_reg_state)
	(*pjsua_var.ua_cfg.cb.on_reg_state)(acc->index);

    /* Check if we need to auto retry registration. Basically, registration
     * failure codes triggering auto-retry are those of temporal failures
     * considered to be recoverable in relatively short term.
     */
    if (acc->cfg.reg_retry_interval && 
	(param->code == PJSIP_SC_REQUEST_TIMEOUT ||
	 param->code == PJSIP_SC_INTERNAL_SERVER_ERROR ||
	 param->code == PJSIP_SC_BAD_GATEWAY ||
	 param->code == PJSIP_SC_SERVICE_UNAVAILABLE ||
	 param->code == PJSIP_SC_SERVER_TIMEOUT ||
	 PJSIP_IS_STATUS_IN_CLASS(param->code, 600))) /* Global failure */
    {
	schedule_reregistration(acc);
    }

    PJSUA_UNLOCK();
}


/*
 * Initialize client registration.
 */
static pj_status_t pjsua_regc_init(int acc_id)
{
    pjsua_acc *acc;
    pj_pool_t *pool;
    pj_status_t status;

    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    if (acc->cfg.reg_uri.slen == 0) {
	PJ_LOG(3,(THIS_FILE, "Registrar URI is not specified"));
	return PJ_SUCCESS;
    }

    /* Destroy existing session, if any */
    if (acc->regc) {
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;
	acc->contact.slen = 0;
    }

    /* initialize SIP registration if registrar is configured */

    status = pjsip_regc_create( pjsua_var.endpt, 
				acc, &regc_cb, &acc->regc);

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create client registration", 
		     status);
	return status;
    }

    pool = pjsua_pool_create("tmpregc", 512, 512);

    if (acc->contact.slen == 0) {
	pj_str_t tmp_contact;

	status = pjsua_acc_create_uac_contact( pool, &tmp_contact,
					       acc_id, &acc->cfg.reg_uri);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to generate suitable Contact header"
				    " for registration", 
			 status);
	    pjsip_regc_destroy(acc->regc);
	    pj_pool_release(pool);
	    acc->regc = NULL;
	    return status;
	}

	pj_strdup_with_null(acc->pool, &acc->contact, &tmp_contact);
    }

    status = pjsip_regc_init( acc->regc,
			      &acc->cfg.reg_uri, 
			      &acc->cfg.id, 
			      &acc->cfg.id,
			      1, &acc->contact, 
			      acc->cfg.reg_timeout);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Client registration initialization error", 
		     status);
	pjsip_regc_destroy(acc->regc);
	pj_pool_release(pool);
	acc->regc = NULL;
	acc->contact.slen = 0;
	return status;
    }

    /* If account is locked to specific transport, then set transport to
     * the client registration.
     */
    if (pjsua_var.acc[acc_id].cfg.transport_id != PJSUA_INVALID_ID) {
	pjsip_tpselector tp_sel;

	pjsua_init_tpselector(pjsua_var.acc[acc_id].cfg.transport_id, &tp_sel);
	pjsip_regc_set_transport(acc->regc, &tp_sel);
    }


    /* Set credentials
     */
    if (acc->cred_cnt) {
	pjsip_regc_set_credentials( acc->regc, acc->cred_cnt, acc->cred);
    }

    /* Set authentication preference */
    pjsip_regc_set_prefs(acc->regc, &acc->cfg.auth_pref);

    /* Set route-set
     */
    if (!pj_list_empty(&acc->route_set)) {
	pjsip_regc_set_route_set( acc->regc, &acc->route_set );
    }

    /* Add other request headers. */
    if (pjsua_var.ua_cfg.user_agent.slen) {
	pjsip_hdr hdr_list;
	const pj_str_t STR_USER_AGENT = { "User-Agent", 10 };
	pjsip_generic_string_hdr *h;

	pj_list_init(&hdr_list);

	h = pjsip_generic_string_hdr_create(pool, &STR_USER_AGENT, 
					    &pjsua_var.ua_cfg.user_agent);
	pj_list_push_back(&hdr_list, (pjsip_hdr*)h);

	pjsip_regc_add_headers(acc->regc, &hdr_list);
    }

    pj_pool_release(pool);

    return PJ_SUCCESS;
}


/*
 * Update registration or perform unregistration. 
 */
PJ_DEF(pj_status_t) pjsua_acc_set_registration( pjsua_acc_id acc_id, 
						pj_bool_t renew)
{
    pj_status_t status = 0;
    pjsip_tx_data *tdata = 0;

    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();

    /* Cancel any re-registration timer */
    pjsua_cancel_timer(&pjsua_var.acc[acc_id].auto_rereg.timer);

    /* Reset pointer to registration transport */
    pjsua_var.acc[acc_id].auto_rereg.reg_tp = NULL;

    if (renew) {
	if (pjsua_var.acc[acc_id].regc == NULL) {
	    status = pjsua_regc_init(acc_id);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Unable to create registration", 
			     status);
		goto on_return;
	    }
	}
	if (!pjsua_var.acc[acc_id].regc) {
	    status = PJ_EINVALIDOP;
	    goto on_return;
	}

	status = pjsip_regc_register(pjsua_var.acc[acc_id].regc, 1, 
				     &tdata);

	if (0 && status == PJ_SUCCESS && pjsua_var.acc[acc_id].cred_cnt) {
	    pjsua_acc *acc = &pjsua_var.acc[acc_id];
	    pjsip_authorization_hdr *h;
	    char *uri;
	    int d;

	    uri = (char*) pj_pool_alloc(tdata->pool, acc->cfg.reg_uri.slen+10);
	    d = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, tdata->msg->line.req.uri,
				uri, acc->cfg.reg_uri.slen+10);
	    pj_assert(d > 0);

	    h = pjsip_authorization_hdr_create(tdata->pool);
	    h->scheme = pj_str("Digest");
	    h->credential.digest.username = acc->cred[0].username;
	    h->credential.digest.realm = acc->srv_domain;
	    h->credential.digest.uri = pj_str(uri);
	    h->credential.digest.algorithm = pj_str("md5");

	    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)h);
	}

    } else {
	if (pjsua_var.acc[acc_id].regc == NULL) {
	    PJ_LOG(3,(THIS_FILE, "Currently not registered"));
	    status = PJ_EINVALIDOP;
	    goto on_return;
	}

	pjsua_pres_unpublish(&pjsua_var.acc[acc_id]);

	status = pjsip_regc_unregister(pjsua_var.acc[acc_id].regc, &tdata);
    }

    if (status == PJ_SUCCESS) {
	//pjsua_process_msg_data(tdata, NULL);
	status = pjsip_regc_send( pjsua_var.acc[acc_id].regc, tdata );
    }

    /* Update pointer to registration transport */
    if (status == PJ_SUCCESS) {
	pjsip_regc_info reg_info;

	pjsip_regc_get_info(pjsua_var.acc[acc_id].regc, &reg_info);
	pjsua_var.acc[acc_id].auto_rereg.reg_tp = reg_info.transport;
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create/send REGISTER", 
		     status);
    } else {
	PJ_LOG(3,(THIS_FILE, "%s sent",
	         (renew? "Registration" : "Unregistration")));
    }

on_return:
    PJSUA_UNLOCK();
    return status;
}


/*
 * Get account information.
 */
PJ_DEF(pj_status_t) pjsua_acc_get_info( pjsua_acc_id acc_id,
					pjsua_acc_info *info)
{
    pjsua_acc *acc = &pjsua_var.acc[acc_id];
    pjsua_acc_config *acc_cfg = &pjsua_var.acc[acc_id].cfg;

    PJ_ASSERT_RETURN(info != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    
    pj_bzero(info, sizeof(pjsua_acc_info));

    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();
    
    if (pjsua_var.acc[acc_id].valid == PJ_FALSE) {
	PJSUA_UNLOCK();
	return PJ_EINVALIDOP;
    }

    info->id = acc_id;
    info->is_default = (pjsua_var.default_acc == acc_id);
    info->acc_uri = acc_cfg->id;
    info->has_registration = (acc->cfg.reg_uri.slen > 0);
    info->online_status = acc->online_status;
    pj_memcpy(&info->rpid, &acc->rpid, sizeof(pjrpid_element));
    if (info->rpid.note.slen)
	info->online_status_text = info->rpid.note;
    else if (info->online_status)
	info->online_status_text = pj_str("Online");
    else
	info->online_status_text = pj_str("Offline");

    if (acc->reg_last_err) {
	info->status = (pjsip_status_code) acc->reg_last_err;
	pj_strerror(acc->reg_last_err, info->buf_, sizeof(info->buf_));
	info->status_text = pj_str(info->buf_);
    } else if (acc->reg_last_code) {
	if (info->has_registration) {
	    info->status = (pjsip_status_code) acc->reg_last_code;
	    info->status_text = *pjsip_get_status_text(acc->reg_last_code);
	} else {
	    info->status = (pjsip_status_code) 0;
	    info->status_text = pj_str("not registered");
	}
    } else if (acc->cfg.reg_uri.slen) {
	info->status = PJSIP_SC_TRYING;
	info->status_text = pj_str("In Progress");
    } else {
	info->status = (pjsip_status_code) 0;
	info->status_text = pj_str("does not register");
    }
    
    if (acc->regc) {
	pjsip_regc_info regc_info;
	pjsip_regc_get_info(acc->regc, &regc_info);
	info->expires = regc_info.next_reg;
    } else {
	info->expires = -1;
    }

    PJSUA_UNLOCK();

    return PJ_SUCCESS;

}


/*
 * Enum accounts all account ids.
 */
PJ_DEF(pj_status_t) pjsua_enum_accs(pjsua_acc_id ids[],
				    unsigned *count )
{
    unsigned i, c;

    PJ_ASSERT_RETURN(ids && *count, PJ_EINVAL);

    PJSUA_LOCK();

    for (i=0, c=0; c<*count && i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	if (!pjsua_var.acc[i].valid)
	    continue;
	ids[c] = i;
	++c;
    }

    *count = c;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*
 * Enum accounts info.
 */
PJ_DEF(pj_status_t) pjsua_acc_enum_info( pjsua_acc_info info[],
					 unsigned *count )
{
    unsigned i, c;

    PJ_ASSERT_RETURN(info && *count, PJ_EINVAL);

    PJSUA_LOCK();

    for (i=0, c=0; c<*count && i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	if (!pjsua_var.acc[i].valid)
	    continue;

	pjsua_acc_get_info(i, &info[c]);
	++c;
    }

    *count = c;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*
 * This is an internal function to find the most appropriate account to
 * used to reach to the specified URL.
 */
PJ_DEF(pjsua_acc_id) pjsua_acc_find_for_outgoing(const pj_str_t *url)
{
    pj_str_t tmp;
    pjsip_uri *uri;
    pjsip_sip_uri *sip_uri;
    pj_pool_t *tmp_pool;
    unsigned i;

    PJSUA_LOCK();

    tmp_pool = pjsua_pool_create("tmpacc10", 256, 256);

    pj_strdup_with_null(tmp_pool, &tmp, url);

    uri = pjsip_parse_uri(tmp_pool, tmp.ptr, tmp.slen, 0);
    if (!uri) {
	pj_pool_release(tmp_pool);
	PJSUA_UNLOCK();
	return pjsua_var.default_acc;
    }

    if (!PJSIP_URI_SCHEME_IS_SIP(uri) && 
	!PJSIP_URI_SCHEME_IS_SIPS(uri)) 
    {
	/* Return the first account with proxy */
	for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	    if (!pjsua_var.acc[i].valid)
		continue;
	    if (!pj_list_empty(&pjsua_var.acc[i].route_set))
		break;
	}

	if (i != PJ_ARRAY_SIZE(pjsua_var.acc)) {
	    /* Found rather matching account */
	    pj_pool_release(tmp_pool);
	    PJSUA_UNLOCK();
	    return i;
	}

	/* Not found, use default account */
	pj_pool_release(tmp_pool);
	PJSUA_UNLOCK();
	return pjsua_var.default_acc;
    }

    sip_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(uri);

    /* Find matching domain AND port */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	if (pj_stricmp(&pjsua_var.acc[acc_id].srv_domain, &sip_uri->host)==0 &&
	    pjsua_var.acc[acc_id].srv_port == sip_uri->port)
	{
	    pj_pool_release(tmp_pool);
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* If no match, try to match the domain part only */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	if (pj_stricmp(&pjsua_var.acc[acc_id].srv_domain, &sip_uri->host)==0)
	{
	    pj_pool_release(tmp_pool);
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }


    /* Still no match, just use default account */
    pj_pool_release(tmp_pool);
    PJSUA_UNLOCK();
    return pjsua_var.default_acc;
}


/*
 * This is an internal function to find the most appropriate account to be
 * used to handle incoming calls.
 */
PJ_DEF(pjsua_acc_id) pjsua_acc_find_for_incoming(pjsip_rx_data *rdata)
{
    pjsip_uri *uri;
    pjsip_sip_uri *sip_uri;
    unsigned i;

    /* Check that there's at least one account configured */
    PJ_ASSERT_RETURN(pjsua_var.acc_cnt!=0, pjsua_var.default_acc);

    uri = rdata->msg_info.to->uri;

    /* Just return default account if To URI is not SIP: */
    if (!PJSIP_URI_SCHEME_IS_SIP(uri) && 
	!PJSIP_URI_SCHEME_IS_SIPS(uri)) 
    {
	return pjsua_var.default_acc;
    }


    PJSUA_LOCK();

    sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);

    /* Find account which has matching username and domain. */
    for (i=0; i < pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	pjsua_acc *acc = &pjsua_var.acc[acc_id];

	if (acc->valid && pj_stricmp(&acc->user_part, &sip_uri->user)==0 &&
	    pj_stricmp(&acc->srv_domain, &sip_uri->host)==0) 
	{
	    /* Match ! */
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* No matching account, try match domain part only. */
    for (i=0; i < pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	pjsua_acc *acc = &pjsua_var.acc[acc_id];

	if (acc->valid && pj_stricmp(&acc->srv_domain, &sip_uri->host)==0) {
	    /* Match ! */
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* No matching account, try match user part (and transport type) only. */
    for (i=0; i < pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	pjsua_acc *acc = &pjsua_var.acc[acc_id];

	if (acc->valid && pj_stricmp(&acc->user_part, &sip_uri->user)==0) {

	    if (acc->cfg.transport_id != PJSUA_INVALID_ID) {
		pjsip_transport_type_e type;
		type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
		if (type == PJSIP_TRANSPORT_UNSPECIFIED)
		    type = PJSIP_TRANSPORT_UDP;

		if (pjsua_var.tpdata[acc->cfg.transport_id].type != type)
		    continue;
	    }

	    /* Match ! */
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* Still no match, use default account */
    PJSUA_UNLOCK();
    return pjsua_var.default_acc;
}


/*
 * Create arbitrary requests for this account. 
 */
PJ_DEF(pj_status_t) pjsua_acc_create_request(pjsua_acc_id acc_id,
					     const pjsip_method *method,
					     const pj_str_t *target,
					     pjsip_tx_data **p_tdata)
{
    pjsip_tx_data *tdata;
    pjsua_acc *acc;
    pjsip_route_hdr *r;
    pj_status_t status;

    PJ_ASSERT_RETURN(method && target && p_tdata, PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);

    acc = &pjsua_var.acc[acc_id];

    status = pjsip_endpt_create_request(pjsua_var.endpt, method, target, 
					&acc->cfg.id, target,
					NULL, NULL, -1, NULL, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create request", status);
	return status;
    }

    /* Copy routeset */
    r = acc->route_set.next;
    while (r != &acc->route_set) {
	pjsip_msg_add_hdr(tdata->msg, 
			  (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, r));
	r = r->next;
    }

    /* If account is locked to specific transport, then set that transport to
     * the transmit data.
     */
    if (pjsua_var.acc[acc_id].cfg.transport_id != PJSUA_INVALID_ID) {
	pjsip_tpselector tp_sel;

	pjsua_init_tpselector(acc->cfg.transport_id, &tp_sel);
	pjsip_tx_data_set_transport(tdata, &tp_sel);
    }

    /* Done */
    *p_tdata = tdata;
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjsua_acc_create_uac_contact( pj_pool_t *pool,
						  pj_str_t *contact,
						  pjsua_acc_id acc_id,
						  const pj_str_t *suri)
{
    pjsua_acc *acc;
    pjsip_sip_uri *sip_uri;
    pj_status_t status;
    pjsip_transport_type_e tp_type = PJSIP_TRANSPORT_UNSPECIFIED;
    pj_str_t local_addr;
    pjsip_tpselector tp_sel;
    unsigned flag;
    int secure;
    int local_port;
    const char *beginquote, *endquote;
    char transport_param[32];

    
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    /* If force_contact is configured, then use use it */
    if (acc->cfg.force_contact.slen) {
	*contact = acc->cfg.force_contact;
	return PJ_SUCCESS;
    }

    /* If route-set is configured for the account, then URI is the 
     * first entry of the route-set.
     */
    if (!pj_list_empty(&acc->route_set)) {
	sip_uri = (pjsip_sip_uri*) 
		  pjsip_uri_get_uri(acc->route_set.next->name_addr.uri);
    } else {
	pj_str_t tmp;
	pjsip_uri *uri;

	pj_strdup_with_null(pool, &tmp, suri);

	uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
	if (uri == NULL)
	    return PJSIP_EINVALIDURI;

	/* For non-SIP scheme, route set should be configured */
	if (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))
	    return PJSIP_EINVALIDREQURI;

	sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);
    }

    /* Get transport type of the URI */
    if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri))
	tp_type = PJSIP_TRANSPORT_TLS;
    else if (sip_uri->transport_param.slen == 0) {
	tp_type = PJSIP_TRANSPORT_UDP;
    } else
	tp_type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
    
    if (tp_type == PJSIP_TRANSPORT_UNSPECIFIED)
	return PJSIP_EUNSUPTRANSPORT;

    /* If destination URI specifies IPv6, then set transport type
     * to use IPv6 as well.
     */
    if (pj_strchr(&sip_uri->host, ':'))
	tp_type = (pjsip_transport_type_e)(((int)tp_type) + PJSIP_TRANSPORT_IPV6);

    flag = pjsip_transport_get_flag_from_type(tp_type);
    secure = (flag & PJSIP_TRANSPORT_SECURE) != 0;

    /* Init transport selector. */
    pjsua_init_tpselector(pjsua_var.acc[acc_id].cfg.transport_id, &tp_sel);

    /* Get local address suitable to send request from */
    status = pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(pjsua_var.endpt),
					 pool, tp_type, &tp_sel, 
					 &local_addr, &local_port);
    if (status != PJ_SUCCESS)
	return status;

    /* Enclose IPv6 address in square brackets */
    if (tp_type & PJSIP_TRANSPORT_IPV6) {
	beginquote = "[";
	endquote = "]";
    } else {
	beginquote = endquote = "";
    }

    /* Don't add transport parameter if it's UDP */
    if (tp_type!=PJSIP_TRANSPORT_UDP && tp_type!=PJSIP_TRANSPORT_UDP6) {
	pj_ansi_snprintf(transport_param, sizeof(transport_param),
		         ";transport=%s",
			 pjsip_transport_get_type_name(tp_type));
    } else {
	transport_param[0] = '\0';
    }


    /* Create the contact header */
    contact->ptr = (char*)pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
    contact->slen = pj_ansi_snprintf(contact->ptr, PJSIP_MAX_URL_SIZE,
				     "%.*s%s<%s:%.*s%s%s%.*s%s:%d%s%.*s>%.*s",
				     (int)acc->display.slen,
				     acc->display.ptr,
				     (acc->display.slen?" " : ""),
				     (secure ? PJSUA_SECURE_SCHEME : "sip"),
				     (int)acc->user_part.slen,
				     acc->user_part.ptr,
				     (acc->user_part.slen?"@":""),
				     beginquote,
				     (int)local_addr.slen,
				     local_addr.ptr,
				     endquote,
				     local_port,
				     transport_param,
				     (int)acc->cfg.contact_uri_params.slen,
				     acc->cfg.contact_uri_params.ptr,
				     (int)acc->cfg.contact_params.slen,
				     acc->cfg.contact_params.ptr);

    return PJ_SUCCESS;
}



PJ_DEF(pj_status_t) pjsua_acc_create_uas_contact( pj_pool_t *pool,
						  pj_str_t *contact,
						  pjsua_acc_id acc_id,
						  pjsip_rx_data *rdata )
{
    /* 
     *  Section 12.1.1, paragraph about using SIPS URI in Contact.
     *  If the request that initiated the dialog contained a SIPS URI 
     *  in the Request-URI or in the top Record-Route header field value, 
     *  if there was any, or the Contact header field if there was no 
     *  Record-Route header field, the Contact header field in the response
     *  MUST be a SIPS URI.
     */
    pjsua_acc *acc;
    pjsip_sip_uri *sip_uri;
    pj_status_t status;
    pjsip_transport_type_e tp_type = PJSIP_TRANSPORT_UNSPECIFIED;
    pj_str_t local_addr;
    pjsip_tpselector tp_sel;
    unsigned flag;
    int secure;
    int local_port;
    const char *beginquote, *endquote;
    char transport_param[32];
    
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    /* If force_contact is configured, then use use it */
    if (acc->cfg.force_contact.slen) {
	*contact = acc->cfg.force_contact;
	return PJ_SUCCESS;
    }

    /* If Record-Route is present, then URI is the top Record-Route. */
    if (rdata->msg_info.record_route) {
	sip_uri = (pjsip_sip_uri*) 
		pjsip_uri_get_uri(rdata->msg_info.record_route->name_addr.uri);
    } else {
	pjsip_hdr *pos = NULL;
	pjsip_contact_hdr *h_contact;
	pjsip_uri *uri = NULL;

	/* Otherwise URI is Contact URI.
	 * Iterate the Contact URI until we find sip: or sips: scheme.
	 */
	do {
	    h_contact = (pjsip_contact_hdr*)
			pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT,
					   pos);
	    if (h_contact) {
		uri = (pjsip_uri*) pjsip_uri_get_uri(h_contact->uri);
		if (!PJSIP_URI_SCHEME_IS_SIP(uri) && 
		    !PJSIP_URI_SCHEME_IS_SIPS(uri))
		{
		    pos = (pjsip_hdr*)h_contact->next;
		    if (pos == &rdata->msg_info.msg->hdr)
			h_contact = NULL;
		} else {
		    break;
		}
	    }
	} while (h_contact);
	

	/* Or if Contact URI is not present, take the remote URI from
	 * the From URI.
	 */
	if (uri == NULL)
	    uri = (pjsip_uri*) pjsip_uri_get_uri(rdata->msg_info.from->uri);


	/* Can only do sip/sips scheme at present. */
	if (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))
	    return PJSIP_EINVALIDREQURI;

	sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);
    }

    /* Get transport type of the URI */
    if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri))
	tp_type = PJSIP_TRANSPORT_TLS;
    else if (sip_uri->transport_param.slen == 0) {
	tp_type = PJSIP_TRANSPORT_UDP;
    } else
	tp_type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);

    if (tp_type == PJSIP_TRANSPORT_UNSPECIFIED)
	return PJSIP_EUNSUPTRANSPORT;

    /* If destination URI specifies IPv6, then set transport type
     * to use IPv6 as well.
     */
    if (pj_strchr(&sip_uri->host, ':'))
	tp_type = (pjsip_transport_type_e)(((int)tp_type) + PJSIP_TRANSPORT_IPV6);

    flag = pjsip_transport_get_flag_from_type(tp_type);
    secure = (flag & PJSIP_TRANSPORT_SECURE) != 0;

    /* Init transport selector. */
    pjsua_init_tpselector(pjsua_var.acc[acc_id].cfg.transport_id, &tp_sel);

    /* Get local address suitable to send request from */
    status = pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(pjsua_var.endpt),
					 pool, tp_type, &tp_sel,
					 &local_addr, &local_port);
    if (status != PJ_SUCCESS)
	return status;

    /* Enclose IPv6 address in square brackets */
    if (tp_type & PJSIP_TRANSPORT_IPV6) {
	beginquote = "[";
	endquote = "]";
    } else {
	beginquote = endquote = "";
    }

    /* Don't add transport parameter if it's UDP */
    if (tp_type!=PJSIP_TRANSPORT_UDP && tp_type!=PJSIP_TRANSPORT_UDP6) {
	pj_ansi_snprintf(transport_param, sizeof(transport_param),
		         ";transport=%s",
			 pjsip_transport_get_type_name(tp_type));
    } else {
	transport_param[0] = '\0';
    }


    /* Create the contact header */
    contact->ptr = (char*) pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
    contact->slen = pj_ansi_snprintf(contact->ptr, PJSIP_MAX_URL_SIZE,
				     "%.*s%s<%s:%.*s%s%s%.*s%s:%d%s%.*s>%.*s",
				     (int)acc->display.slen,
				     acc->display.ptr,
				     (acc->display.slen?" " : ""),
				     (secure ? PJSUA_SECURE_SCHEME : "sip"),
				     (int)acc->user_part.slen,
				     acc->user_part.ptr,
				     (acc->user_part.slen?"@":""),
				     beginquote,
				     (int)local_addr.slen,
				     local_addr.ptr,
				     endquote,
				     local_port,
				     transport_param,
				     (int)acc->cfg.contact_uri_params.slen,
				     acc->cfg.contact_uri_params.ptr,
				     (int)acc->cfg.contact_params.slen,
				     acc->cfg.contact_params.ptr);

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjsua_acc_set_transport( pjsua_acc_id acc_id,
					     pjsua_transport_id tp_id)
{
    pjsua_acc *acc;

    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    PJ_ASSERT_RETURN(tp_id >= 0 && tp_id < (int)PJ_ARRAY_SIZE(pjsua_var.tpdata),
		     PJ_EINVAL);
    
    acc->cfg.transport_id = tp_id;

    return PJ_SUCCESS;
}


/* Auto re-registration timeout callback */
static void auto_rereg_timer_cb(pj_timer_heap_t *th, pj_timer_entry *te)
{
    pjsua_acc *acc;
    pj_status_t status;

    PJ_UNUSED_ARG(th);
    acc = (pjsua_acc*) te->user_data;
    pj_assert(acc);

    PJSUA_LOCK();

    if (!acc->valid || !acc->auto_rereg.active)
	goto on_return;

    /* Start re-registration */
    acc->auto_rereg.attempt_cnt++;
    status = pjsua_acc_set_registration(acc->index, PJ_TRUE);
    if (status != PJ_SUCCESS)
	schedule_reregistration(acc);

on_return:
    PJSUA_UNLOCK();
}


/* Schedule reregistration for specified account. Note that the first 
 * re-registration after a registration failure will be done immediately.
 * Also note that this function should be called within PJSUA mutex.
 */
static void schedule_reregistration(pjsua_acc *acc)
{
    pj_time_val delay;

    pj_assert(acc && acc->valid && acc->cfg.reg_retry_interval);

    /* If configured, disconnect calls of this account after the first
     * reregistration attempt failed.
     */
    if (acc->cfg.drop_calls_on_reg_fail && acc->auto_rereg.attempt_cnt >= 1)
    {
	unsigned i, cnt;

	for (i = 0, cnt = 0; i < pjsua_var.ua_cfg.max_calls; ++i) {
	    if (pjsua_var.calls[i].acc_id == acc->index) {
		pjsua_call_hangup(i, 0, NULL, NULL);
		++cnt;
	    }
	}

	if (cnt) {
	    PJ_LOG(3, (THIS_FILE, "Disconnecting %d call(s) of account #%d "
				  "after reregistration attempt failed",
				  cnt, acc->index));
	}
    }

    /* Cancel any re-registration timer */
    pjsua_cancel_timer(&acc->auto_rereg.timer);

    /* Update re-registration flag */
    acc->auto_rereg.active = PJ_TRUE;

    /* Set up timer for reregistration */
    acc->auto_rereg.timer.cb = &auto_rereg_timer_cb;
    acc->auto_rereg.timer.user_data = acc;

    /* Reregistration attempt. The first attempt will be done immediately. */
    delay.sec = acc->auto_rereg.attempt_cnt? acc->cfg.reg_retry_interval : 0;
    delay.msec = 0;
    pjsua_schedule_timer(&acc->auto_rereg.timer, &delay);
}


/* Internal function to perform auto-reregistration on transport 
 * connection/disconnection events.
 */
void pjsua_acc_on_tp_state_changed(pjsip_transport *tp,
				   pjsip_transport_state state,
				   const pjsip_transport_state_info *info)
{
    unsigned i;

    PJ_UNUSED_ARG(info);

    /* Only care for transport disconnection events */
    if (state != PJSIP_TP_STATE_DISCONNECTED)
	return;

    /* Shutdown this transport, to make sure that the transport manager 
     * will create a new transport for reconnection.
     */
    pjsip_transport_shutdown(tp);

    PJSUA_LOCK();

    /* Enumerate accounts using this transport and perform actions
     * based on the transport state.
     */
    for (i = 0; i < PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	pjsua_acc *acc = &pjsua_var.acc[i];

	/* Skip if this account is not valid OR auto re-registration
	 * feature is disabled OR this transport is not used by this account.
	 */
	if (!acc->valid || !acc->cfg.reg_retry_interval || 
	    tp != acc->auto_rereg.reg_tp)
	{
	    continue;
	}

	/* Schedule reregistration for this account */
	schedule_reregistration(acc);
    }

    PJSUA_UNLOCK();
}
