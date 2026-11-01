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
 * Verify that async entry retains ordinary pointer, callback-target, and
 * discriminant bindings without inheriting or returning caller lock state.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct async_entry_state {
	mutex_t lock;
	int data;
};

static mutex_t async_entry_caller_lock;
static int async_entry_caller_data;
static struct async_entry_state async_entry_object;

_NOTE(MUTEX_PROTECTS_DATA(async_entry_caller_lock,
    async_entry_caller_data))
_NOTE(MUTEX_PROTECTS_DATA(async_entry_state::lock,
    async_entry_state::data))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
async_entry_callback(struct async_entry_state *state, int selected)
{
	state->data++;
	async_entry_caller_data = selected;
}

static void
async_worker(struct async_entry_state *state, int selected,
    void (*callback)(struct async_entry_state *, int))
{
	if (selected)
		mutex_enter(&state->lock);
	callback(state, selected);
}

_NOTE(ASYNC_ENTRY(async_worker))

void
async_entry_root(void)
{
	mutex_enter(&async_entry_caller_lock);
	async_worker(&async_entry_object, 1, async_entry_callback);
	async_entry_caller_data++;
	mutex_exit(&async_entry_caller_lock);

	mutex_enter(&async_entry_object.lock);
	mutex_exit(&async_entry_object.lock);
}
