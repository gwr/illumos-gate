/*
 * Verify that an annotation name which remains unresolved after macro
 * expansion reports the expanded spelling.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct macro_error_state {
	mutex_t lock;
	int value;
} macro_error_state_t;

#define	MACRO_MISSING_MEMBER	missing_member

_NOTE(MUTEX_PROTECTS_DATA(macro_error_state::lock,
    macro_error_state::MACRO_MISSING_MEMBER))
