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
 * Exercise object identities that are clearer in Sparse's computed addresses
 * than in the retained source expressions.  Each pair keeps the protected
 * member fixed while changing whether the acquired lock belongs to that
 * exact object.  This distinguishes exact address reasoning from rules that
 * merely equate source roots, member names, or types.
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
 * Sparse substitutes the copied pointer into the lowered lock argument.  The
 * same case therefore uses one computed object address despite the different
 * source spellings alias and state.  Rebinding alias to other keeps distinct
 * computed addresses and ensures that recognizing pointer copies does not
 * equate arbitrary objects of the same type.
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
 * Constant indices are represented by byte displacement from states.  This
 * checks both recovery of the repeated alias_state owner and preservation of
 * the selected element.
 *
 * A symbolic index instead produces a computed element address.  Reusing one
 * index reuses that address; independent indices produce distinct Sparse
 * pseudos even though both source expressions have the root states.  The
 * latter case catches identity schemes that discard the subscript.
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
 * The container conversion subtracts link's constant offset from an embedded
 * link pointer.  When link came from container, normalizing those constant
 * additions and casts recovers the original container address.  Starting
 * from other must retain that unrelated address; matching only the recovered
 * type and member would incorrectly make container->lock protect it.
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
