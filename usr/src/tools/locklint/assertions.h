#ifndef LOCKLINT_ASSERTIONS_H
#define	LOCKLINT_ASSERTIONS_H

struct instruction;
struct locklint_access;

enum locklint_assertion {
	LOCKLINT_ASSERT_NONE,
	LOCKLINT_ASSERT_HELD,
	LOCKLINT_ASSERT_NOT_HELD
};

void locklint_assertions_enable(void);
enum locklint_assertion locklint_get_assertion(struct instruction *,
    struct locklint_access *);

#endif
