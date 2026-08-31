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
 * Characterize exact object identity through local aliases, aliased formal
 * arguments, array elements, and a representative container conversion.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct alias_state {
	mutex_t lock;
	int value;
} alias_state_t;

typedef struct alias_link {
	struct alias_link *next;
} alias_link_t;

typedef struct alias_container {
	int prefix;
	alias_link_t link;
	mutex_t lock;
	int value;
} alias_container_t;

_NOTE(MUTEX_PROTECTS_DATA(alias_state::lock, alias_state::value))
_NOTE(MUTEX_PROTECTS_DATA(alias_container::lock, alias_container::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

#define	ALIAS_OFFSETOF(type, member) \
	((unsigned long)&(((type *)0)->member))
#define	ALIAS_CONTAINER_OF(pointer, type, member) \
	((type *)((char *)(pointer) - ALIAS_OFFSETOF(type, member)))

/*
 * A direct copy denotes the same object.  Rebinding the copy to another
 * object must not make that other object's lock protect the original.
 */
static int
direct_alias_same(alias_state_t *state)
{
	alias_state_t *alias = state;
	int value;

	mutex_enter(&alias->lock);
	value = state->value;
	mutex_exit(&alias->lock);
	return (value);
}

static int
direct_alias_different(alias_state_t *state, alias_state_t *other)
{
	alias_state_t *alias = other;
	int value;

	mutex_enter(&alias->lock);
	value = state->value;
	mutex_exit(&alias->lock);
	return (value);
}

/*
 * Equal constant or symbolic indices identify the same element.  Different
 * constant indices are distinct, and unrelated symbolic indices must not be
 * assumed equal.
 */
static int
array_same_constant(alias_state_t *states)
{
	int value;

	mutex_enter(&states[0].lock);
	value = states[0].value;
	mutex_exit(&states[0].lock);
	return (value);
}

static int
array_different_constant(alias_state_t *states)
{
	int value;

	mutex_enter(&states[0].lock);
	value = states[1].value;
	mutex_exit(&states[0].lock);
	return (value);
}

static int
array_same_symbolic(alias_state_t *states, unsigned int index)
{
	int value;

	mutex_enter(&states[index].lock);
	value = states[index].value;
	mutex_exit(&states[index].lock);
	return (value);
}

static int
array_different_symbolic(alias_state_t *states, unsigned int first,
    unsigned int second)
{
	int value;

	mutex_enter(&states[first].lock);
	value = states[second].value;
	mutex_exit(&states[first].lock);
	return (value);
}

/*
 * Recovering the enclosing object from its embedded link preserves identity
 * only when the link was derived from that exact object.
 */
static int
container_alias_same(alias_container_t *container)
{
	alias_link_t *link = &container->link;
	alias_container_t *alias =
	    ALIAS_CONTAINER_OF(link, alias_container_t, link);
	int value;

	mutex_enter(&container->lock);
	value = alias->value;
	mutex_exit(&container->lock);
	return (value);
}

static int
container_alias_different(alias_container_t *container,
    alias_link_t *other)
{
	alias_container_t *alias =
	    ALIAS_CONTAINER_OF(other, alias_container_t, link);
	int value;

	mutex_enter(&container->lock);
	value = alias->value;
	mutex_exit(&container->lock);
	return (value);
}
