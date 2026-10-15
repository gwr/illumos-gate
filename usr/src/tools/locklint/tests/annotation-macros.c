/*
 * Verify that ordinary object-like macros expand throughout declarative
 * annotation names before those names are parsed and resolved.  The cases
 * cover complete lock and data names, one path component, a generated group,
 * nested macro expansion, and scheme protection.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct macro_data {
	int complete;
	int component;
	int grouped_first;
	int grouped_second;
	int nested;
	int scheme;
} macro_data_t;

typedef struct macro_state {
	mutex_t lock;
	macro_data_t data;
} macro_state_t;

#define	MACRO_LOCK		macro_state::lock
#define	MACRO_COMPLETE_DATA	macro_state::data.complete
#define	MACRO_COMPONENT		data.component
#define	MACRO_GROUP		{ grouped_first grouped_second }
#define	MACRO_NESTED		MACRO_NESTED_INNER
#define	MACRO_NESTED_INNER	macro_state::data.nested
#define	MACRO_SCHEME_DATA	macro_state::data.scheme

_NOTE(MUTEX_PROTECTS_DATA(MACRO_LOCK, MACRO_COMPLETE_DATA))
_NOTE(MUTEX_PROTECTS_DATA(macro_state::lock,
    macro_state::MACRO_COMPONENT))
_NOTE(MUTEX_PROTECTS_DATA(macro_state::lock,
    macro_state::data.MACRO_GROUP))
_NOTE(MUTEX_PROTECTS_DATA(macro_state::lock, MACRO_NESTED))
_NOTE(SCHEME_PROTECTS_DATA("external scheme", MACRO_SCHEME_DATA))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

void
check_annotation_macros(macro_state_t *state)
{
	state->data.complete = 1;
	state->data.component = 1;
	state->data.grouped_first = 1;
	state->data.grouped_second = 1;
	state->data.nested = 1;
	state->data.scheme = 1;

	mutex_enter(&state->lock);
	state->data.complete = 2;
	state->data.component = 2;
	state->data.grouped_first = 2;
	state->data.grouped_second = 2;
	state->data.nested = 2;
	mutex_exit(&state->lock);
}
