#ifndef LOCKLINT_EVENTS_H
#define	LOCKLINT_EVENTS_H

struct entrypoint;
struct instruction;
struct locklint_access;

enum locklint_lock_action {
	LOCKLINT_LOCK_NONE,
	LOCKLINT_LOCK_ACQUIRE,
	LOCKLINT_LOCK_RELEASE
};

enum locklint_lock_action locklint_get_lock_action(struct instruction *,
    struct locklint_access *);
void locklint_show_events(struct entrypoint *);

#endif
