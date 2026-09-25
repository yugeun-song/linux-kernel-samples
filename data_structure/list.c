// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>

#define MAX_USERNAME_LENGTH 32
#define MAX_CART_SIZE 10
#define N_USER_INFOS 5

enum food_code {
	BANANA = 0,
	KIWI,
	STRAWBERRY,
	BREAD,
	MILK,
	HAM,
	N_FOOD_CODES,
};

static const char *const food_names[N_FOOD_CODES] = {
	[BANANA] = "banana",
	[KIWI] = "kiwi",
	[STRAWBERRY] = "strawberry",
	[BREAD] = "bread",
	[MILK] = "milk",
	[HAM] = "ham",
};

struct user_info {
	struct list_head list_node;
	enum food_code shopping_cart[MAX_CART_SIZE];
	u8 age;
	u8 n_cart_items;
	char username[MAX_USERNAME_LENGTH];
};

static LIST_HEAD(g_user_info_list);

static inline const char *get_food_name(enum food_code food)
{
	if ((unsigned int)food >= N_FOOD_CODES)
		return "unknown";

	return food_names[food];
}

static inline enum food_code pick_random_food(void)
{
	return (enum food_code)get_random_u32_below(N_FOOD_CODES);
}

static struct user_info *get_user_info(const char *username)
{
	struct list_head *pos;
	struct user_info *user;

	if (!username)
		return NULL;

	list_for_each(pos, &g_user_info_list) {
		user = container_of(pos, struct user_info, list_node);

		if (!strcmp(user->username, username))
			return user;
	}

	return NULL;
}

static inline void print_shopping_cart(const struct user_info *user)
{
	char cart[128] = { [0] = '\0' };
	size_t len = 0;
	unsigned int i;

	for (i = 0; i < user->n_cart_items; i++)
		len += scnprintf(cart + len, sizeof(cart) - len, "%s%s",
				 i ? ", " : "", get_food_name(user->shopping_cart[i]));

	pr_info("  cart(%u/%u): %s\n", user->n_cart_items, MAX_CART_SIZE,
		user->n_cart_items ? cart : "(empty)");
}

static void print_all_user_infos(void)
{
	struct user_info *user;

	pr_info("%zu user infos in the list\n", list_count_nodes(&g_user_info_list));

	if (list_empty(&g_user_info_list))
		return;

	pr_info("first=%s last=%s\n",
		list_first_entry(&g_user_info_list, struct user_info, list_node)->username,
		list_last_entry(&g_user_info_list, struct user_info, list_node)->username);

	list_for_each_entry(user, &g_user_info_list, list_node) {
		pr_info("username=%s age=%u\n", user->username, user->age);
		print_shopping_cart(user);
	}
}

static int delete_user_info(const char *username)
{
	struct user_info *user;

	user = get_user_info(username);
	if (!user)
		return -ENOENT;

	list_del(&user->list_node);
	kfree(user);

	return 0;
}

static void free_all_user_infos(void)
{
	struct user_info *user;
	struct user_info *tmp;

	list_for_each_entry_safe(user, tmp, &g_user_info_list, list_node) {
		list_del(&user->list_node);
		kfree(user);
	}
}

static int __init user_info_init(void)
{
	static const char *const get_targets[] = { "username#2", "username#4", "nobody" };
	static const char *const delete_targets[] = { "username#1", "username#1", "username#4", "nobody" };
	struct user_info *user;
	unsigned int i;
	unsigned int j;
	int ret;

	pr_info("start linked list example code\n");

	for (i = 0; i < N_USER_INFOS; i++) {
		user = kzalloc(sizeof(*user), GFP_KERNEL);
		if (!user) {
			ret = -ENOMEM;
			goto out_free;
		}

		scnprintf(user->username, sizeof(user->username), "username#%u", i);
		user->age = get_random_u32_below(100) + 1;
		user->n_cart_items = get_random_u32_below(MAX_CART_SIZE + 1);

		for (j = 0; j < user->n_cart_items; j++)
			user->shopping_cart[j] = pick_random_food();

		list_add_tail(&user->list_node, &g_user_info_list);
	}

	print_all_user_infos();

	for (i = 0; i < ARRAY_SIZE(get_targets); i++) {
		user = get_user_info(get_targets[i]);

		if (user)
			pr_info("'%s' is in the list and is %u years old\n", get_targets[i], user->age);
		else
			pr_info("'%s' is not in the list\n", get_targets[i]);
	}

	user = get_user_info("username#3");
	if (user) {
		pr_info("username#3 is %u years old, changing the age to 99\n", user->age);

		user->age = 99;
		pr_info("username#3 is now %u years old\n", user->age);

		user->shopping_cart[0] = MILK;
		user->shopping_cart[1] = HAM;
		user->n_cart_items = 2;
		pr_info("replaced the cart of username#3 with milk and ham\n");
	}

	for (i = 0; i < ARRAY_SIZE(delete_targets); i++) {
		ret = delete_user_info(delete_targets[i]);
		if (ret)
			pr_info("there is no '%s' to delete (%pe)\n", delete_targets[i], ERR_PTR(ret));
		else
			pr_info("deleted '%s'\n", delete_targets[i]);
	}

	print_all_user_infos();

	return 0;

out_free:
	free_all_user_infos();

	return ret;
}

static void __exit user_info_exit(void)
{
	free_all_user_infos();
	pr_info("finished linked list example code\n");
}

module_init(user_info_init);
module_exit(user_info_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("linked list sample in linux kernel");
MODULE_VERSION("1.0");
