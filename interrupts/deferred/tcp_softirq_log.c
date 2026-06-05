// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": %s() - " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/printk.h>
#include <linux/atomic.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/preempt.h>
#include <net/net_namespace.h>

#define SAMPLE_EVERY 16

static atomic_long_t seen = ATOMIC_LONG_INIT(0);
static atomic_long_t captured = ATOMIC_LONG_INIT(0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
static unsigned int tcp_softirq_hook(void *priv, struct sk_buff *skb,
				     const struct nf_hook_state *state)
#else
static unsigned int tcp_softirq_hook(const struct nf_hook_ops *ops,
				     struct sk_buff *skb,
				     const struct nf_hook_state *state)
#endif
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	unsigned long n, c;

	if (!skb)
		return NF_ACCEPT;
	iph = ip_hdr(skb);
	if (!iph || iph->protocol != IPPROTO_TCP)
		return NF_ACCEPT;

	n = atomic_long_inc_return(&seen);
	if (n % SAMPLE_EVERY != 0)
		return NF_ACCEPT;

	c = atomic_long_inc_return(&captured);
	tcph = tcp_hdr(skb);
	pr_info("captured #%lu/%lu: %pI4:%u -> %pI4:%u  in_hardirq=%s in_softirq=%s in_task=%s\n",
		c, n, &iph->saddr, ntohs(tcph->source), &iph->daddr,
		ntohs(tcph->dest), in_hardirq() ? "Y" : "N",
		in_softirq() ? "Y" : "N", in_task() ? "Y" : "N");
	return NF_ACCEPT;
}

static struct nf_hook_ops tcp_hook = {
	.hook = tcp_softirq_hook,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FIRST,
};

static int __init tcp_softirq_init(void)
{
	pr_info("init: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N",
		in_task() ? "Y" : "N");
	pr_info("LOCAL_IN hook: logs every %u-th TCP packet, always NF_ACCEPT (observe-only)\n",
		SAMPLE_EVERY);
	return nf_register_net_hook(&init_net, &tcp_hook);
}

static void __exit tcp_softirq_exit(void)
{
	pr_info("exit: in_hardirq=%s in_softirq=%s in_task=%s\n",
		in_hardirq() ? "Y" : "N", in_softirq() ? "Y" : "N",
		in_task() ? "Y" : "N");
	nf_unregister_net_hook(&init_net, &tcp_hook);
	pr_info("unloaded; seen=%lu captured=%lu\n", atomic_long_read(&seen),
		atomic_long_read(&captured));
}

module_init(tcp_softirq_init);
module_exit(tcp_softirq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Logs every Nth TCP packet via a netfilter hook (observe-only)");
MODULE_VERSION("1.0");
