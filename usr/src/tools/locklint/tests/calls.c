#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct call_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(call_state::lock, call_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
check_callee(struct call_state *state)
{
	return (state->value);
}

static int
check_locked_caller(struct call_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = check_callee(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
check_unlocked_caller(struct call_state *state)
{
	return (check_callee(state));
}

static int
check_wrapper(struct call_state *state)
{
	return (check_callee(state));
}

static int
check_locked_wrapper_caller(struct call_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = check_wrapper(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
check_unlocked_wrapper_caller(struct call_state *state)
{
	return (check_wrapper(state));
}

static int
check_recursive(struct call_state *state, int depth)
{
	if (depth != 0)
		return (check_recursive(state, depth - 1));
	return (state->value);
}

static int
check_recursive_caller(struct call_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = check_recursive(state, 2);
	mutex_exit(&state->lock);
	return (value);
}

static int
check_isolated_recursive(struct call_state *state, int depth)
{
	if (depth != 0)
		return (check_isolated_recursive(state, depth - 1));
	return (state->value);
}
