#define	_NOTE(arg)
#define	_OTHER(arg)
#define	LOCK_MEMBER	lock

typedef struct event_state {
	int value;
	int count;
	int lock;
} event_state;

_OTHER(MUTEX_PROTECTS_DATA(ignored::lock, ignored::value))
_NOTE(READ_ONLY_DATA(event_state::LOCK_MEMBER))
_NOTE(MUTEX_PROTECTS_DATA(event_state::lock,
    event_state::{ value count }))
