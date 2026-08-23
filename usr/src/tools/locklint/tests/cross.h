#ifndef LOCKLINT_TEST_CROSS_H
#define	LOCKLINT_TEST_CROSS_H

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct cross_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(cross_state::lock, cross_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern int cross_locked(struct cross_state *);
extern int cross_unlocked(struct cross_state *);
extern int cross_effect(struct cross_state *);
extern int cross_read(struct cross_state *);
extern void cross_acquire(struct cross_state *);

#endif
