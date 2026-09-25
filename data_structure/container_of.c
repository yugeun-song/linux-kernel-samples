// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/container_of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/stddef.h>
#include <linux/types.h>

#define pr_check(expr) pr_info("  %-44s : %s\n", #expr, (expr) ? "yes" : "no")

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

	u8 *header_ptr;
	u64 *value_ptr;
	int *nested_value_ptr;
	u16 *tail_ptr;
	struct inner_struct *recovered_nested;
	struct some_struct *from_header;
	struct some_struct *from_value;
	struct some_struct *from_nested;
	struct some_struct *from_nested_value;
	struct some_struct *from_tail;
	const struct some_struct *const_obj = &obj;
	const struct inner_struct *const_recovered_nested;
	const struct some_struct *const_from_header;
	const struct some_struct *const_from_value;
	const struct some_struct *const_from_nested;
	const struct some_struct *const_from_nested_value;
	const struct some_struct *const_from_tail;

	pr_info("container_of example loaded\n");

	pr_info("obj = %px\n", &obj);
	pr_info("struct some_struct:\n");
	pr_info("  offsetof(header)         = %zu\n", offsetof(struct some_struct, header));
	pr_info("  offsetof(aligned_value)  = %zu\n", offsetof(struct some_struct, aligned_value));
	pr_info("  offsetof(nested)         = %zu\n", offsetof(struct some_struct, nested));
	pr_info("  offsetof(nested.marker)  = %zu\n", offsetof(struct some_struct, nested.marker));
	pr_info("  offsetof(nested.value)   = %zu\n", offsetof(struct some_struct, nested.value));
	pr_info("  offsetof(nested.payload) = %zu\n", offsetof(struct some_struct, nested.payload));
	pr_info("  offsetof(name)           = %zu\n", offsetof(struct some_struct, name));
	pr_info("  offsetof(tail)           = %zu\n", offsetof(struct some_struct, tail));
	pr_info("  offsetofend(tail)        = %zu\n", offsetofend(struct some_struct, tail));
	pr_info("  sizeof                   = %zu\n", sizeof(struct some_struct));
	pr_info("struct inner_struct:\n");
	pr_info("  offsetofend(payload)     = %zu\n", offsetofend(struct inner_struct, payload));
	pr_info("  sizeof                   = %zu\n", sizeof(struct inner_struct));

	value_ptr = &obj.aligned_value;
	from_value = container_of(value_ptr, struct some_struct, aligned_value);
#ifdef CONTAINER_OF_TYPE_MISMATCH
	from_value = container_of(value_ptr, struct some_struct, tail);
#endif

	pr_info("[1] direct member\n");
	pr_info("  value_ptr     = %px\n", value_ptr);
	pr_info("  from_value    = %px\n", from_value);
	pr_info("  name          = %s\n", from_value->name);
	pr_info("  aligned_value = 0x%llx\n", from_value->aligned_value);

	nested_value_ptr = &obj.nested.value;
	recovered_nested = container_of(nested_value_ptr, struct inner_struct, value);
	from_nested = container_of(recovered_nested, struct some_struct, nested);
	from_nested_value = container_of(nested_value_ptr, struct some_struct, nested.value);

	pr_info("[2] nested structure\n");
	pr_info("  nested_value_ptr  = %px\n", nested_value_ptr);
	pr_info("  recovered_nested  = %px\n", recovered_nested);
	pr_info("  from_nested       = %px\n", from_nested);
	pr_info("  from_nested_value = %px\n", from_nested_value);
	pr_info("  marker            = %c\n", recovered_nested->marker);
	pr_info("  value             = %d\n", recovered_nested->value);
	pr_info("  payload           = %d, %d, %d\n", recovered_nested->payload[0],
		recovered_nested->payload[1], recovered_nested->payload[2]);
	pr_info("  name              = %s\n", from_nested->name);

	tail_ptr = &obj.tail;
	from_tail = container_of(tail_ptr, struct some_struct, tail);

	pr_info("[3] member near the end\n");
	pr_info("  tail_ptr  = %px\n", tail_ptr);
	pr_info("  from_tail = %px\n", from_tail);
	pr_info("  tail      = 0x%x\n", from_tail->tail);
	pr_info("  name      = %s\n", from_tail->name);

	header_ptr = &obj.header;
	from_header = container_of(header_ptr, struct some_struct, header);

	pr_info("[4] member at offset 0\n");
	pr_info("  header_ptr  = %px\n", header_ptr);
	pr_info("  from_header = %px\n", from_header);
	pr_info("  header      = 0x%x\n", from_header->header);
	pr_info("  name        = %s\n", from_header->name);
	pr_check((struct some_struct *)header_ptr == &obj);
	pr_check((struct some_struct *)value_ptr == &obj);

	const_from_value =
		container_of_const(&const_obj->aligned_value, struct some_struct, aligned_value);
	const_recovered_nested =
		container_of_const(&const_obj->nested.value, struct inner_struct, value);
	const_from_nested = container_of_const(const_recovered_nested, struct some_struct, nested);
	const_from_nested_value =
		container_of_const(&const_obj->nested.value, struct some_struct, nested.value);
	const_from_tail = container_of_const(&const_obj->tail, struct some_struct, tail);
	const_from_header = container_of_const(&const_obj->header, struct some_struct, header);
#ifdef CONTAINER_OF_CONST_LOSS
	container_of(&const_obj->tail, struct some_struct, tail)->tail = 0;
	container_of_const(&const_obj->tail, struct some_struct, tail)->tail = 0;
#endif

	pr_info("[5] container_of_const on a const view\n");
	pr_check(const_from_value == from_value);
	pr_check(const_recovered_nested == recovered_nested);
	pr_check(const_from_nested == from_nested);
	pr_check(const_from_nested_value == from_nested_value);
	pr_check(const_from_tail == from_tail);
	pr_check(const_from_header == from_header);

	pr_info("[check]\n");
	pr_check(from_value == &obj);
	pr_check(recovered_nested == &obj.nested);
	pr_check(from_nested == &obj);
	pr_check(from_nested_value == &obj);
	pr_check(from_tail == &obj);
	pr_check(from_header == &obj);

	return 0;
}

static void __exit container_of_example_exit(void)
{
	pr_info("container_of example unloaded\n");
}

module_init(container_of_example_init);
module_exit(container_of_example_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Struct recovery from member pointers with container_of and container_of_const");
MODULE_VERSION("1.0");
