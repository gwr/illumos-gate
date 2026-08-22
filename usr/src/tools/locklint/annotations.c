#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "annotations.h"
#include "token.h"

struct annotation_token {
	struct position pos;
	char *text;
	struct annotation_token *next;
};

struct annotation {
	struct position pos;
	struct annotation_token *tokens;
	struct annotation *next;
};

static struct annotation *annotations;
static struct annotation **annotations_tail = &annotations;

static char *
copy_string(const char *text)
{
	char *copy;
	size_t size;

	size = strlen(text) + 1;
	copy = malloc(size);
	if (copy == NULL)
		die("out of memory recording locklint annotation");
	(void) memcpy(copy, text, size);
	return (copy);
}

static const struct token *
annotation_end(const struct token *open)
{
	const struct token *token;
	unsigned int depth = 1;

	for (token = open->next; !eof_token(token); token = token->next) {
		if (token_type(token) == TOKEN_SPECIAL &&
		    token->special == '(') {
			depth++;
		} else if (token_type(token) == TOKEN_SPECIAL &&
		    token->special == ')' && --depth == 0) {
			return (token);
		}
	}

	return (NULL);
}

static void
capture_annotation(const struct token *macro,
    const struct token *open, void *data)
{
	struct annotation *annotation;
	struct annotation_token **tail;
	const struct token *end;
	const struct token *token;

	(void) data;

	end = annotation_end(open);
	if (end == NULL)
		return;

	annotation = calloc(1, sizeof (*annotation));
	if (annotation == NULL)
		die("out of memory recording locklint annotation");
	annotation->pos = macro->pos;
	tail = &annotation->tokens;

	for (token = open->next; token != end; token = token->next) {
		struct annotation_token *copy;

		copy = calloc(1, sizeof (*copy));
		if (copy == NULL)
			die("out of memory recording locklint annotation token");
		copy->pos = token->pos;
		copy->text = copy_string(show_token(token));
		*tail = copy;
		tail = &copy->next;
	}

	*annotations_tail = annotation;
	annotations_tail = &annotation->next;
}

void
locklint_annotations_enable(void)
{
	add_macro_expansion_hook("_NOTE", capture_annotation, NULL);
}

void
locklint_show_annotations(FILE *stream)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_token *token;
		bool first = true;

		(void) fprintf(stream, "%s:%u:%u: _NOTE(",
		    stream_name(annotation->pos.stream),
		    annotation->pos.line, annotation->pos.pos);
		for (token = annotation->tokens; token != NULL;
		    token = token->next) {
			if (!first &&
			    (token->pos.whitespace || token->pos.newline))
				(void) fputc(' ', stream);
			(void) fputs(token->text, stream);
			first = false;
		}
		(void) fputs(")\n", stream);
	}
}
