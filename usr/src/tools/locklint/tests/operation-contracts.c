/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify contract consistency for callbacks assigned while constructing a
 * returned operation vector.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

struct operation_contract_object {
	mutex_t lock;
};

struct operation_contract_private {
	struct operation_contract_object *object;
};

struct operation_contract_info {
	struct operation_contract_private *private;
};

typedef void (*operation_contract_function_t)(
    struct operation_contract_object *);
typedef void (*operation_contract_fini_t)(struct operation_contract_info *);

struct operation_contracts {
	operation_contract_function_t start;
	operation_contract_function_t finish;
	operation_contract_fini_t fini;
};

extern struct operation_contracts *allocate_operation_contracts(void);
extern int operation_contract_condition(void);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
void operation_contract_balanced(struct operation_contract_info *);
struct operation_contracts *make_operation_contracts(void);

static void
operation_contract_acquire(struct operation_contract_object *object)
{
	mutex_enter(&object->lock);
}

static void
operation_contract_none(struct operation_contract_object *object)
{
	(void) object;
}

static void
operation_contract_conditional_helper(struct operation_contract_private *private)
{
	if (operation_contract_condition())
		return;
	(void) private;
}

void
operation_contract_balanced(struct operation_contract_info *info)
{
	struct operation_contract_private *private = info->private;
	struct operation_contract_object *object = private->object;

	mutex_enter(&object->lock);
	operation_contract_conditional_helper(private);
	mutex_exit(&object->lock);
}

struct operation_contracts *
make_operation_contracts(void)
{
	struct operation_contracts *operations =
	    allocate_operation_contracts();

	operations->start = operation_contract_acquire;
	operations->finish = operation_contract_none;
	operations->fini = operation_contract_balanced;
	return (operations);
}
