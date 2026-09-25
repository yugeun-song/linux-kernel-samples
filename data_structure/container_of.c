// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/container_of.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/types.h>

struct inner_struct {
	char marker;
	int value;
	char payload[3];
};

struct some_struct {
	u8 header;
	u64 aligned_value;
	struct inner_struct nested;
	char name[16];
	u16 tail;
};

static int __init container_of_example_init(void)
{
	struct some_struct obj = {
		.header = 0xaa,
		.aligned_value = 0x123456789abcdef0ULL,
		.nested = {
			.marker = 'X',
			.value = 1234,
			.payload = { 1, 2, 3 },
		},
		.name = "hello",
		.tail = 0xbeef,
	};

	u64 *value_ptr;
	int *nested_value_ptr;
	u16 *tail_ptr;
	struct inner_struct *recovered_nested;
	struct some_struct *from_value;
	struct some_struct *from_nested;
	struct some_struct *from_tail;

	pr_info("container_of example loaded\n");

	pr_info("obj = %px\n", &obj);
	pr_info("some_struct:\n");
	pr_info("  offsetof(header)         = %zu\n", offsetof(struct some_struct, header));
	pr_info("  offsetof(aligned_value)  = %zu\n", offsetof(struct some_struct, aligned_value));
	pr_info("  offsetof(nested)         = %zu\n", offsetof(struct some_struct, nested));
	pr_info("  offsetof(nested.marker)  = %zu\n", offsetof(struct some_struct, nested.marker));
	pr_info("  offsetof(nested.value)   = %zu\n", offsetof(struct some_struct, nested.value));
	pr_info("  offsetof(nested.payload) = %zu\n", offsetof(struct some_struct, nested.payload));
	pr_info("  offsetof(name)           = %zu\n", offsetof(struct some_struct, name));
	pr_info("  offsetof(tail)           = %zu\n", offsetof(struct some_struct, tail));

	value_ptr = &obj.aligned_value;
	from_value = container_of(value_ptr, struct some_struct, aligned_value);

	pr_info("[1] direct member\n");
	pr_info("  value_ptr     = %px\n", value_ptr);
	pr_info("  recovered     = %px\n", from_value);
	pr_info("  name          = %s\n", from_value->name);
	pr_info("  aligned_value = 0x%llx\n", from_value->aligned_value);

	nested_value_ptr = &obj.nested.value;
	recovered_nested = container_of(nested_value_ptr, struct inner_struct, value);
	from_nested = container_of(recovered_nested, struct some_struct, nested);

	pr_info("[2] nested structure\n");
	pr_info("  nested.value     = %px\n", nested_value_ptr);
	pr_info("  recovered_nested = %px\n", recovered_nested);
	pr_info("  recovered_outer  = %px\n", from_nested);
	pr_info("  marker           = %c\n", recovered_nested->marker);
	pr_info("  value            = %d\n", recovered_nested->value);
	pr_info("  payload          = %d, %d, %d\n", recovered_nested->payload[0],
		recovered_nested->payload[1], recovered_nested->payload[2]);
	pr_info("  name             = %s\n", from_nested->name);

	tail_ptr = &obj.tail;
	from_tail = container_of(tail_ptr, struct some_struct, tail);

	pr_info("[3] member near the end\n");
	pr_info("  tail_ptr  = %px\n", tail_ptr);
	pr_info("  recovered = %px\n", from_tail);
	pr_info("  tail      = 0x%x\n", from_tail->tail);
	pr_info("  name      = %s\n", from_tail->name);

	pr_info("[check]\n");
	pr_info("  from_value == &obj              : %s\n", from_value == &obj ? "YES" : "NO");
	pr_info("  from_nested == &obj             : %s\n", from_nested == &obj ? "YES" : "NO");
	pr_info("  from_tail == &obj               : %s\n", from_tail == &obj ? "YES" : "NO");
	pr_info("  recovered_nested == &obj.nested : %s\n", recovered_nested == &obj.nested ? "YES" : "NO");

	return 0;
}

static void __exit container_of_example_exit(void)
{
	pr_info("container_of example unloaded\n");
}

module_init(container_of_example_init);
module_exit(container_of_example_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("container_of example");
MODULE_VERSION("1.0");
