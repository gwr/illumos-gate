#include "cross.h"

int
cross_read(struct cross_state *state)
{
	return (state->value);
}

void
cross_acquire(struct cross_state *state)
{
	mutex_enter(&state->lock);
}
