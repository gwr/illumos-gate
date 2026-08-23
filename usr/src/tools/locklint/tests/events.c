/*
 * _NOTE discards an argument that intentionally contains non-C tokens.
 */
#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct event_state {
	int value;
	int other;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(event_state::lock, event_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void event_helper(struct event_state *);

static int
event_sequence(struct event_state *state)
{
	int value;

	value = state->value;
	value += state->other;
	mutex_enter(&state->lock);
	state->value = value;
	event_helper(state);
	mutex_exit(&state->lock);

	return (state->value);
}
