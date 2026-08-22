#define	_NOTE(arg)
#define	_OTHER(arg)
#define	LOCK_MEMBER	lock

_OTHER(MUTEX_PROTECTS_DATA(ignored::lock, ignored::value))
_NOTE(MUTEX_PROTECTS_DATA(event_state::LOCK_MEMBER,
    event_state::{ value count }))
