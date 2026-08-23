#include "cross.h"

int
cross_locked(struct cross_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = cross_read(state);
	mutex_exit(&state->lock);
	return (value);
}

int
cross_unlocked(struct cross_state *state)
{
	return (cross_read(state));
}

int
cross_effect(struct cross_state *state)
{
	int value;

	cross_acquire(state);
	value = cross_read(state);
	mutex_exit(&state->lock);
	return (value);
}
