/*
 * Verify that ASSERT and VERIFY recognize lock predicates introduced by
 * ordinary one-level and nested function-like macro expansion.
 */

#define	ASSERT(expr)
#define	VERIFY(expr)
#define	MUTEX_HELD(lock)	mutex_owned(lock)
#define	RW_READ_HELD(lock)	rw_read_held(lock)
#define	RW_WRITE_HELD(lock)	rw_write_held(lock)
#define	RW_LOCK_HELD(lock)	rw_lock_held(lock)

#define	MUTEX_WRAPPER(lock)	MUTEX_HELD(lock)
#define	NESTED_MUTEX_WRAPPER(lock)	MUTEX_WRAPPER(lock)
#define	READ_WRAPPER(lock)	RW_READ_HELD(lock)
#define	WRITE_WRAPPER(lock)	RW_WRITE_HELD(lock)
#define	RW_WRAPPER(lock)	RW_LOCK_HELD(lock)

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;

struct assertion_macro_state {
	mutex_t mutex;
	rwlock_t rwlock;
};

extern int mutex_owned(mutex_t *);
extern int rw_read_held(rwlock_t *);
extern int rw_write_held(rwlock_t *);
extern int rw_lock_held(rwlock_t *);

static void
require_nested_mutex(struct assertion_macro_state *state)
{
	ASSERT(NESTED_MUTEX_WRAPPER(&state->mutex));
}

static void
verify_mutex(struct assertion_macro_state *state)
{
	VERIFY(MUTEX_WRAPPER(&state->mutex));
}

static void
verify_mutex_direct(struct assertion_macro_state *state)
{
	VERIFY(MUTEX_HELD(&state->mutex));
}

static void
require_read(struct assertion_macro_state *state)
{
	ASSERT(READ_WRAPPER(&state->rwlock));
}

static void
require_write(struct assertion_macro_state *state)
{
	ASSERT(WRITE_WRAPPER(&state->rwlock));
}

static void
require_rw(struct assertion_macro_state *state)
{
	ASSERT(RW_WRAPPER(&state->rwlock));
}

static void
require_rw_direct(struct assertion_macro_state *state)
{
	ASSERT(RW_LOCK_HELD(&state->rwlock));
}

void
call_nested_mutex_unheld(struct assertion_macro_state *state)
{
	require_nested_mutex(state);
}

void
call_verify_mutex_unheld(struct assertion_macro_state *state)
{
	verify_mutex(state);
}

void
call_verify_mutex_direct_unheld(struct assertion_macro_state *state)
{
	verify_mutex_direct(state);
}

void
call_read_unheld(struct assertion_macro_state *state)
{
	require_read(state);
}

void
call_write_unheld(struct assertion_macro_state *state)
{
	require_write(state);
}

void
call_rw_unheld(struct assertion_macro_state *state)
{
	require_rw(state);
}

void
call_rw_direct_unheld(struct assertion_macro_state *state)
{
	require_rw_direct(state);
}
