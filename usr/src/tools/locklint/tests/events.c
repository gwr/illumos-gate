typedef struct mutex {
	int opaque;
} mutex_t;

struct event_state {
	int value;
	mutex_t lock;
};

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void event_helper(struct event_state *);

static int
event_sequence(struct event_state *state)
{
	int value;

	value = state->value;
	mutex_enter(&state->lock);
	state->value = value;
	event_helper(state);
	mutex_exit(&state->lock);

	return (state->value);
}
