#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct user_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(user_state::lock, user_state::value))

extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);
extern int user_mutex_unlocked(struct user_state *);
extern int user_mutex_locked(struct user_state *);
extern void user_mutex_acquire(struct user_state *);
extern int user_mutex_call_effect(struct user_state *);

int
user_mutex_unlocked(struct user_state *state)
{
	return (state->value);
}

int
user_mutex_locked(struct user_state *state)
{
	int value;

	(void) mutex_lock(&state->lock);
	value = state->value;
	(void) mutex_unlock(&state->lock);
	return (value);
}

void
user_mutex_acquire(struct user_state *state)
{
	(void) mutex_lock(&state->lock);
}

int
user_mutex_call_effect(struct user_state *state)
{
	int value;

	user_mutex_acquire(state);
	value = state->value;
	(void) mutex_unlock(&state->lock);
	return (value);
}
