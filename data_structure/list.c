/* SPDX-License-Identifier: 0BSD */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/array_size.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <linux/types.h>

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

struct user_info {
	struct list_head list_node;
	enum food_code shopping_cart[MAX_CART_SIZE];
	u8 age;
	size_t n_cart_items;
	char username[MAX_USERNAME_LENGTH];
};

static LIST_HEAD(gs_user_info_list);

static inline const char *get_food_name(enum food_code food)
{
	static const char *const s_food_names[N_FOOD_CODES] = {
		[BANANA] = "banana",
		[KIWI] = "kiwi",
		[STRAWBERRY] = "strawberry",
		[BREAD] = "bread",
		[MILK] = "milk",
		[HAM] = "ham",
	};

	if ((unsigned int)food >= N_FOOD_CODES) {
		return "unknown";
	}

	return s_food_names[food];
}

static inline enum food_code pick_random_food(void)
{
	return (enum food_code)get_random_u32_below(N_FOOD_CODES);
}

static struct user_info *get_user_info(const char *const username_nullable)
{
	if (username_nullable == NULL) {
		return NULL;
	}

	struct list_head *pos;

	list_for_each(pos, &gs_user_info_list) {
		struct user_info *const user = container_of(pos, struct user_info, list_node);

		if (strcmp(user->username, username_nullable) == 0) {
			return user;
		}
	}

	return NULL;
}

static void print_shopping_cart(const struct user_info *const user)
{
	char cart[128] = { 0 };
	size_t len = 0;

	for (size_t i = 0; i < user->n_cart_items; ++i) {
		len += (size_t)scnprintf(cart + len, sizeof(cart) - len, "%s%s",
					 (i != 0) ? ", " : "",
					 get_food_name(user->shopping_cart[i]));
	}

	pr_info("  cart(%zu/%u): %s\n", user->n_cart_items, MAX_CART_SIZE,
		(user->n_cart_items != 0) ? cart : "(empty)");
}

static void print_all_user_infos(void)
{
	pr_info("%zu user infos in the list\n", list_count_nodes(&gs_user_info_list));

	if (list_empty(&gs_user_info_list)) {
		return;
	}

	const struct user_info *const first = list_first_entry(&gs_user_info_list, struct user_info,
							       list_node);
	const struct user_info *const last = list_last_entry(&gs_user_info_list, struct user_info,
							     list_node);

	pr_info("first=%s last=%s\n", first->username, last->username);

	const struct user_info *user;

	list_for_each_entry(user, &gs_user_info_list, list_node) {
		pr_info("username=%s age=%u\n", user->username, user->age);
		print_shopping_cart(user);
	}
}

static int delete_user_info(const char *const username_nullable)
{
	struct user_info *user = get_user_info(username_nullable);
	if (user == NULL) {
		return -ENOENT;
	}

	list_del(&user->list_node);
	kfree(user);
	user = NULL;

	return 0;
}

static void free_all_user_infos(void)
{
	struct user_info *user;
	struct user_info *tmp;

	list_for_each_entry_safe(user, tmp, &gs_user_info_list, list_node) {
		list_del(&user->list_node);
		kfree(user);
		user = NULL;
	}
}

static int __init init_list_module(void)
{
	static const char *const s_get_targets[] = {
		"username#2",
		"username#4",
		"nobody",
	};
	static const char *const s_delete_targets[] = {
		"username#1",
		"username#1",
		"username#4",
		"nobody",
	};

	for (size_t i = 0; i < N_USER_INFOS; ++i) {
		struct user_info *const user = kzalloc(sizeof(*user), GFP_KERNEL);
		if (user == NULL) {
			goto err_free_user_infos;
		}

		scnprintf(user->username, sizeof(user->username), "username#%zu", i);
		user->age = (u8)(get_random_u32_below(100) + 1);
		user->n_cart_items = get_random_u32_below(MAX_CART_SIZE + 1);

		for (size_t j = 0; j < user->n_cart_items; ++j) {
			user->shopping_cart[j] = pick_random_food();
		}

		list_add_tail(&user->list_node, &gs_user_info_list);
	}

	print_all_user_infos();

	for (size_t i = 0; i < ARRAY_SIZE(s_get_targets); ++i) {
		const struct user_info *const user = get_user_info(s_get_targets[i]);

		if (user != NULL) {
			pr_info("'%s' is in the list and is %u years old\n", s_get_targets[i],
				user->age);
		} else {
			pr_info("'%s' is not in the list\n", s_get_targets[i]);
		}
	}

	struct user_info *const user = get_user_info("username#3");
	if (user != NULL) {
		pr_info("username#3 is %u years old, changing the age to 99\n", user->age);

		user->age = 99;
		pr_info("username#3 is now %u years old\n", user->age);

		user->shopping_cart[0] = MILK;
		user->shopping_cart[1] = HAM;
		user->n_cart_items = 2;
		pr_info("replaced the cart of username#3 with milk and ham\n");
	}

	for (size_t i = 0; i < ARRAY_SIZE(s_delete_targets); ++i) {
		int ret = delete_user_info(s_delete_targets[i]);

		if (ret != 0) {
			pr_info("there is no '%s' to delete (%pe)\n", s_delete_targets[i],
				ERR_PTR(ret));
		} else {
			pr_info("deleted '%s'\n", s_delete_targets[i]);
		}
	}

	print_all_user_infos();

	pr_info("loaded\n");
	return 0;

err_free_user_infos:
	free_all_user_infos();

	return -ENOMEM;
}

static void __exit exit_list_module(void)
{
	free_all_user_infos();
	pr_info("unloaded\n");
}

module_init(init_list_module);
module_exit(exit_list_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Linked list on list_head with lookup, update and delete");
MODULE_VERSION("1.0");
