/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright Authors of Cilium */

#ifndef __LIB_FIB_H_
#define __LIB_FIB_H_

#include <bpf/ctx/ctx.h>
#include <bpf/api.h>

#include "common.h"
#include "neigh.h"
#include "l3.h"

#ifdef ENABLE_IPV6
static __always_inline int
fib_redirect_v6(struct __ctx_buff *ctx __maybe_unused,
		int l3_off __maybe_unused,
		struct ipv6hdr *ip6 __maybe_unused, int *oif)
{
	bool no_neigh = false;
	struct bpf_redir_neigh *nh = NULL;
	struct bpf_redir_neigh nh_params;
	struct bpf_fib_lookup fib_params = {
		.family		= AF_INET6,
		.ifindex	= ctx->ingress_ifindex,
	};
	int ret;

	ipv6_addr_copy((union v6addr *)&fib_params.ipv6_src,
		       (union v6addr *)&ip6->saddr);
	ipv6_addr_copy((union v6addr *)&fib_params.ipv6_dst,
		       (union v6addr *)&ip6->daddr);

	ret = fib_lookup(ctx, &fib_params, sizeof(fib_params),
			 BPF_FIB_LOOKUP_DIRECT);
	switch (ret) {
	case BPF_FIB_LKUP_RET_SUCCESS:
		break;
	case BPF_FIB_LKUP_RET_NO_NEIGH:
		nh_params.nh_family = fib_params.family;
		__bpf_memcpy_builtin(&nh_params.ipv6_nh, &fib_params.ipv6_dst,
				     sizeof(nh_params.ipv6_nh));
		no_neigh = true;
		nh = &nh_params;
		break;
	default:
		return CTX_ACT_DROP;
	}

	*oif = fib_params.ifindex;

	ret = ipv6_l3(ctx, l3_off, NULL, NULL, METRIC_EGRESS);
	if (unlikely(ret != CTX_ACT_OK))
		return ret;
	if (no_neigh) {
		if (neigh_resolver_available()) {
			if (nh)
				return redirect_neigh(*oif, nh, sizeof(*nh), 0);
			else
				return redirect_neigh(*oif, NULL, 0, 0);
		} else {
			union macaddr *dmac, smac =
				NATIVE_DEV_MAC_BY_IFINDEX(fib_params.ifindex);
			dmac = nh && nh_params.nh_family == AF_INET ?
			       neigh_lookup_ip4(&fib_params.ipv4_dst) :
			       neigh_lookup_ip6((void *)&fib_params.ipv6_dst);
			if (!dmac)
				return CTX_ACT_DROP;
			if (eth_store_daddr_aligned(ctx, dmac->addr, 0) < 0)
				return CTX_ACT_DROP;
			if (eth_store_saddr_aligned(ctx, smac.addr, 0) < 0)
				return CTX_ACT_DROP;
		}
	} else {
		if (eth_store_daddr(ctx, fib_params.dmac, 0) < 0)
			return CTX_ACT_DROP;
		if (eth_store_saddr(ctx, fib_params.smac, 0) < 0)
			return CTX_ACT_DROP;
	}
	return ctx_redirect(ctx, *oif, 0);
}
#endif /* ENABLE_IPV6 */

#ifdef ENABLE_IPV4
static __always_inline int
fib_redirect_v4(struct __ctx_buff *ctx __maybe_unused,
		int l3_off __maybe_unused,
		struct iphdr *ip4 __maybe_unused, int *oif)
{
	bool no_neigh = false;
	struct bpf_redir_neigh *nh = NULL;
	struct bpf_redir_neigh nh_params;
	struct bpf_fib_lookup fib_params = {
		.family		= AF_INET,
		.ifindex	= ctx->ingress_ifindex,
		.ipv4_src	= ip4->saddr,
		.ipv4_dst	= ip4->daddr,
	};
	int ret;

	ret = fib_lookup(ctx, &fib_params, sizeof(fib_params),
			 BPF_FIB_LOOKUP_DIRECT);
	switch (ret) {
	case BPF_FIB_LKUP_RET_SUCCESS:
		break;
	case BPF_FIB_LKUP_RET_NO_NEIGH:
		/* GW could also be v6, so copy union. */
		nh_params.nh_family = fib_params.family;
		__bpf_memcpy_builtin(&nh_params.ipv6_nh, &fib_params.ipv6_dst,
				     sizeof(nh_params.ipv6_nh));
		no_neigh = true;
		nh = &nh_params;
		break;
	default:
		return CTX_ACT_DROP;
	}

	*oif = fib_params.ifindex;

	ret = ipv4_l3(ctx, l3_off, NULL, NULL, ip4);
	if (unlikely(ret != CTX_ACT_OK))
		return ret;
	if (no_neigh) {
		if (neigh_resolver_available()) {
			if (nh)
				return redirect_neigh(*oif, nh, sizeof(*nh), 0);
			else
				return redirect_neigh(*oif, NULL, 0, 0);
		} else {
			union macaddr *dmac, smac =
				NATIVE_DEV_MAC_BY_IFINDEX(fib_params.ifindex);
			dmac = nh && nh_params.nh_family == AF_INET6 ?
			       neigh_lookup_ip6((void *)&fib_params.ipv6_dst) :
			       neigh_lookup_ip4(&fib_params.ipv4_dst);
			if (!dmac)
				return CTX_ACT_DROP;
			if (eth_store_daddr_aligned(ctx, dmac->addr, 0) < 0)
				return CTX_ACT_DROP;
			if (eth_store_saddr_aligned(ctx, smac.addr, 0) < 0)
				return CTX_ACT_DROP;
		}
	} else {
		if (eth_store_daddr(ctx, fib_params.dmac, 0) < 0)
			return CTX_ACT_DROP;
		if (eth_store_saddr(ctx, fib_params.smac, 0) < 0)
			return CTX_ACT_DROP;
	}
	return ctx_redirect(ctx, *oif, 0);
}
#endif /* ENABLE_IPV4 */
#endif /* __LIB_FIB_H_ */
