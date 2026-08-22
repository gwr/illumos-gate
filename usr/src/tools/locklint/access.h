#ifndef LOCKLINT_ACCESS_H
#define	LOCKLINT_ACCESS_H

#include <stdio.h>

struct expression;

void locklint_show_access(FILE *, struct expression *);

#endif
