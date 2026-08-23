#define	_NOTE(arg)
#define	ASSERT(expr)
#define	VERIFY(expr)	((void)(expr))
#define	MUTEX_HELD(lock)	mutex_owned(lock)
#define	MUTEX_NOT_HELD(lock)	(!mutex_owned(lock))

typedef struct mutex {
	int opaque;
} mutex_t;

struct assertion_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(assertion_state::lock,
    assertion_state::value))

extern int mutex_owned(mutex_t *);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern int assertion_unprotected(struct assertion_state *);
extern int assertion_macro_held(struct assertion_state *);
extern int assertion_direct_held(struct assertion_state *);
extern int assertion_macro_not_held(struct assertion_state *);
extern int assertion_negated(struct assertion_state *);
extern int assertion_zero_comparison(struct assertion_state *);
extern int assertion_active_held(struct assertion_state *);

int
assertion_unprotected(struct assertion_state *state)
{
	return (state->value);
}

int
assertion_macro_held(struct assertion_state *state)
{
	ASSERT(MUTEX_HELD(&state->lock));
	return (state->value);
}

int
assertion_direct_held(struct assertion_state *state)
{
	VERIFY(mutex_owned(&state->lock));
	return (state->value);
}

int
assertion_macro_not_held(struct assertion_state *state)
{
	mutex_enter(&state->lock);
	ASSERT(MUTEX_NOT_HELD(&state->lock));
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
	return (0);
}

int
assertion_negated(struct assertion_state *state)
{
	mutex_enter(&state->lock);
	ASSERT(!mutex_owned(&state->lock));
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
	return (0);
}

int
assertion_zero_comparison(struct assertion_state *state)
{
	mutex_enter(&state->lock);
	ASSERT(mutex_owned(&state->lock) == 0);
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
	return (0);
}

#undef	ASSERT
#define	ASSERT(expr)	((void)((expr) || assfail()))

extern int assfail(void);

int
assertion_active_held(struct assertion_state *state)
{
	ASSERT(MUTEX_HELD(&state->lock));
	return (state->value);
}
