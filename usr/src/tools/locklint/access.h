#ifndef LOCKLINT_ACCESS_H
#define	LOCKLINT_ACCESS_H

#include <stdbool.h>
#include <stdio.h>

struct expression;
struct symbol;

struct locklint_access {
	struct symbol *root;
	struct symbol *member;
};

bool locklint_get_access(struct expression *, struct locklint_access *);
void locklint_show_access(FILE *, struct expression *);

#endif
