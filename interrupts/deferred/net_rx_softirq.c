/* SPDX-License-Identifier: 0BSD */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/smp.h>
#include <linux/tcp.h>
#include <net/net_namespace.h>

#define SAMPLE_EVERY 16

static atomic_long_t gs_seen_count = ATOMIC_LONG_INIT(0);
static atomic_long_t gs_captured_count = ATOMIC_LONG_INIT(0);

static unsigned int run_net_rx_softirq_hook(void *const mut_priv_nullable,
					    struct sk_buff *const mut_skb_nullable,
					    const struct nf_hook_state *const state)
{
	if (mut_skb_nullable == NULL) {
		return NF_ACCEPT;
	}

	struct iphdr *iph = ip_hdr(mut_skb_nullable);
	if (iph == NULL || iph->protocol != IPPROTO_TCP) {
		return NF_ACCEPT;
	}

	size_t n = (size_t)atomic_long_fetch_inc(&gs_seen_count);
	if (n % SAMPLE_EVERY != 0) {
		return NF_ACCEPT;
	}

	struct tcphdr tcph_buffer;
	struct tcphdr *tcph = skb_header_pointer(mut_skb_nullable,
						 skb_transport_offset(mut_skb_nullable),
						 sizeof(tcph_buffer), &tcph_buffer);
	if (tcph == NULL) {
		return NF_ACCEPT;
	}
	atomic_long_inc(&gs_captured_count);
	pr_info("CPU#%u captured #%zu: %pI4:%u -> %pI4:%u  in_hardirq=%s in_softirq=%s in_serving_softirq=%s in_task=%s\n",
		smp_processor_id(), n, &iph->saddr, ntohs(tcph->source), &iph->daddr,
		ntohs(tcph->dest), (in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_serving_softirq() != 0) ? "Y" : "N", (in_task() != 0) ? "Y" : "N");
	return NF_ACCEPT;
}

static struct nf_hook_ops gs_tcp_hook = {
	.hook = run_net_rx_softirq_hook,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FIRST
};

static int __init init_net_rx_softirq_module(void)
{
	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");

	int ret = nf_register_net_hook(&init_net, &gs_tcp_hook);
	if (ret == 0) {
		pr_info("loaded; LOCAL_IN hook logs every %u-th TCP packet, always NF_ACCEPT (observe-only)\n",
			SAMPLE_EVERY);
	}
	return ret;
}

static void __exit exit_net_rx_softirq_module(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		(in_hardirq() != 0) ? "Y" : "N", (in_softirq() != 0) ? "Y" : "N",
		(in_task() != 0) ? "Y" : "N");
	/*
	 * nf_unregister_net_hook() unlinks the hook and then
	 * synchronize_net()s: it waits for any hook still running on another
	 * CPU to finish before returning. So once it returns,
	 * run_net_rx_softirq_hook can no longer be entered, and the module unloads
	 * with no use-after-free even if packets were arriving during rmmod --
	 * hence no extra teardown is needed.
	 */
	nf_unregister_net_hook(&init_net, &gs_tcp_hook);
	pr_info("unloaded; gs_seen_count=%lu gs_captured_count=%lu\n",
		atomic_long_read(&gs_seen_count), atomic_long_read(&gs_captured_count));
}

module_init(init_net_rx_softirq_module);
module_exit(exit_net_rx_softirq_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Netfilter hook logging every Nth TCP packet (observe-only)");
MODULE_VERSION("1.0");
