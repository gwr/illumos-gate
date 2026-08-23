#ifndef LOCKLINT_ANNOTATIONS_H
#define	LOCKLINT_ANNOTATIONS_H

#include <stdio.h>

struct symbol;

void locklint_annotations_enable(void);
struct symbol *locklint_protecting_member(struct symbol *);
void locklint_resolve_annotations(void);
void locklint_show_annotations(FILE *);

#endif
