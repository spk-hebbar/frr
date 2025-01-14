// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP RD definitions for BGP-based VPNs (IP/EVPN)
 * -- brought over from bgpd/bgp_mplsvpn.c
 * Copyright (C) 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 */

#include <zebra.h>
#include "command.h"
#include "log.h"
#include "prefix.h"
#include "memory.h"
#include "stream.h"
#include "filter.h"
#include "frrstr.h"

#include "lib/printfrr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_rd.h"
#include "bgpd/bgp_attr.h"

#ifdef ENABLE_BGP_VNC
#include "bgpd/rfapi/rfapi_backend.h"
#endif

uint16_t decode_rd_type(const uint8_t *pnt)
{
	uint16_t v;

	v = ((uint16_t)*pnt++ << 8);
#ifdef ENABLE_BGP_VNC
	/*
	 * VNC L2 stores LHI in lower byte, so omit it
	 */
	if (v != RD_TYPE_VNC_ETH)
		v |= (uint16_t)*pnt;
#else /* duplicate code for clarity */
	v |= (uint16_t)*pnt;
#endif
	return v;
}

void encode_rd_type(uint16_t v, uint8_t *pnt)
{
	*((uint16_t *)pnt) = htons(v);
}

/* type == RD_TYPE_AS */
void decode_rd_as(const uint8_t *pnt, struct rd_as *rd_as)
{
	rd_as->as = (uint16_t)*pnt++ << 8;
	rd_as->as |= (uint16_t)*pnt++;
	ptr_get_be32(pnt, &rd_as->val);
}

/* type == RD_TYPE_AS4 */
void decode_rd_as4(const uint8_t *pnt, struct rd_as *rd_as)
{
	pnt = ptr_get_be32(pnt, &rd_as->as);
	rd_as->val = ((uint16_t)*pnt++ << 8);
	rd_as->val |= (uint16_t)*pnt;
}

/* type == RD_TYPE_IP */
void decode_rd_ip(const uint8_t *pnt, struct rd_ip *rd_ip)
{
	memcpy(&rd_ip->ip, pnt, 4);
	pnt += 4;

	rd_ip->val = ((uint16_t)*pnt++ << 8);
	rd_ip->val |= (uint16_t)*pnt;
}

#ifdef ENABLE_BGP_VNC
/* type == RD_TYPE_VNC_ETH */
void decode_rd_vnc_eth(const uint8_t *pnt, struct rd_vnc_eth *rd_vnc_eth)
{
	rd_vnc_eth->type = RD_TYPE_VNC_ETH;
	rd_vnc_eth->local_nve_id = pnt[1];
	memcpy(rd_vnc_eth->macaddr.octet, pnt + 2, ETH_ALEN);
}
#endif

int str2prefix_rd(const char *str, struct prefix_rd *prd)
{
	int ret = 0;
	char *p;
	char *p2;
	struct stream *s = NULL;
	char *half = NULL;
	struct in_addr addr;

	prd->family = AF_UNSPEC;
	prd->prefixlen = 64;

	p = strchr(str, ':');
	if (!p)
		goto out;

	if (!all_digit(p + 1))
		goto out;

	s = stream_new(RD_BYTES);

	half = XMALLOC(MTYPE_TMP, (p - str) + 1);
	memcpy(half, str, (p - str));
	half[p - str] = '\0';

	p2 = strchr(str, '.');

	if (!p2) {
		unsigned long as_val;

		if (!all_digit(half))
			goto out;

		as_val = atol(half);
		if (as_val > 0xffff) {
			stream_putw(s, RD_TYPE_AS4);
			stream_putl(s, as_val);
			stream_putw(s, atol(p + 1));
		} else {
			stream_putw(s, RD_TYPE_AS);
			stream_putw(s, as_val);
			stream_putl(s, atol(p + 1));
		}
	} else {
		if (!inet_aton(half, &addr))
			goto out;

		stream_putw(s, RD_TYPE_IP);
		stream_put_in_addr(s, &addr);
		stream_putw(s, atol(p + 1));
	}
	memcpy(prd->val, s->data, 8);
	ret = 1;

out:
	if (s)
		stream_free(s);
	XFREE(MTYPE_TMP, half);
	return ret;
}

char *prefix_rd2str(const struct prefix_rd *prd, char *buf, size_t size)
{
	const uint8_t *pnt;
	uint16_t type;
	struct rd_as rd_as;
	struct rd_ip rd_ip;

	assert(size >= RD_ADDRSTRLEN);

	pnt = prd->val;

	type = decode_rd_type(pnt);

	if (type == RD_TYPE_AS) {
		decode_rd_as(pnt + 2, &rd_as);
		snprintf(buf, size, "%u:%u", rd_as.as, rd_as.val);
		return buf;
	} else if (type == RD_TYPE_AS4) {
		decode_rd_as4(pnt + 2, &rd_as);
		snprintf(buf, size, "%u:%u", rd_as.as, rd_as.val);
		return buf;
	} else if (type == RD_TYPE_IP) {
		decode_rd_ip(pnt + 2, &rd_ip);
		snprintfrr(buf, size, "%pI4:%hu", &rd_ip.ip, rd_ip.val);
		return buf;
	}
#ifdef ENABLE_BGP_VNC
	else if (type == RD_TYPE_VNC_ETH) {
		snprintf(buf, size, "LHI:%d, %02x:%02x:%02x:%02x:%02x:%02x",
			 *(pnt + 1), /* LHI */
			 *(pnt + 2), /* MAC[0] */
			 *(pnt + 3), *(pnt + 4), *(pnt + 5), *(pnt + 6),
			 *(pnt + 7));

		return buf;
	}
#endif

	snprintf(buf, size, "Unknown Type: %d", type);
	return buf;
}

void form_auto_rd(struct in_addr router_id,
		  uint16_t rd_id,
		  struct prefix_rd *prd)
{
	char buf[100];

	prd->family = AF_UNSPEC;
	prd->prefixlen = 64;
	snprintfrr(buf, sizeof(buf), "%pI4:%hu", &router_id, rd_id);
	(void)str2prefix_rd(buf, prd);
}

printfrr_ext_autoreg_p("RD", printfrr_prd);
static ssize_t printfrr_prd(struct fbuf *buf, struct printfrr_eargs *ea,
			    const void *ptr)
{
	char rd_buf[RD_ADDRSTRLEN];

	if (!ptr)
		return bputs(buf, "(null)");

	prefix_rd2str(ptr, rd_buf, sizeof(rd_buf));

	return bputs(buf, rd_buf);
}
