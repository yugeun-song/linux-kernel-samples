// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/container_of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/stddef.h>
#include <linux/types.h>

struct inner_struct {
	char marker;
	int value;
};

struct some_struct {
	u8 header;
	u64 aligned_value;
	struct inner_struct nested_value;
	u16 tail;
};

static int __init container_of_module_init(void)
{
	struct some_struct obj = {
		.header = 0xaa,
		.aligned_value = 0x123456789abcdef0ULL,
		.nested_value = {
			.marker = 'X',
			.value = 1234,
		},
		.tail = 0xbeef,
	};
	u64 *value_ptr = &obj.aligned_value;
	u16 *tail_ptr = &obj.tail;
	int *inner_value_ptr = &obj.nested_value.value;
	struct some_struct *from_value;
	struct some_struct *from_tail;
	struct some_struct *from_inner_value;

	from_value = container_of(value_ptr, struct some_struct, aligned_value);
	from_tail = container_of(tail_ptr, struct some_struct, tail);
	from_inner_value = container_of(inner_value_ptr, struct some_struct, nested_value.value);

	pr_info("obj = %px, nested_value at offset %zu\n", &obj,
		offsetof(struct some_struct, nested_value));

	pr_info("[1] aligned_value, before nested_value\n");
	pr_info("  value_ptr  = %px\n", value_ptr);
	pr_info("  offsetof   = %zu\n", offsetof(struct some_struct, aligned_value));
	pr_info("  from_value = %px\n", from_value);

	pr_info("[2] tail, after nested_value\n");
	pr_info("  tail_ptr  = %px\n", tail_ptr);
	pr_info("  offsetof  = %zu\n", offsetof(struct some_struct, tail));
	pr_info("  from_tail = %px\n", from_tail);

	pr_info("[3] nested_value.value, inside nested_value\n");
	pr_info("  inner_value_ptr  = %px\n", inner_value_ptr);
	pr_info("  offsetof         = %zu\n", offsetof(struct some_struct, nested_value.value));
	pr_info("  from_inner_value = %px\n", from_inner_value);

	pr_info("[check]\n");
	pr_info("  from_value == &obj       : %s\n", from_value == &obj ? "yes" : "no");
	pr_info("  from_tail == &obj        : %s\n", from_tail == &obj ? "yes" : "no");
	pr_info("  from_inner_value == &obj : %s\n", from_inner_value == &obj ? "yes" : "no");

	pr_info("loaded\n");
	return 0;
}

static void __exit container_of_module_exit(void)
{
	pr_info("unloaded\n");
}

module_init(container_of_module_init);
module_exit(container_of_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Struct recovery from member pointers with container_of");
MODULE_VERSION("1.0");
