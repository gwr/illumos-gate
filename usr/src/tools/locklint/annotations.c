#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "annotations.h"
#include "expression.h"
#include "symbol.h"
#include "token.h"

struct annotation_token {
	struct position pos;
	enum token_type type;
	char *text;
	struct annotation_token *next;
};

struct member_ref {
	struct position pos;
	char *type_name;
	char *member_name;
	struct symbol *type;
	struct symbol *member;
	int offset;
	struct member_ref *next;
};

struct annotation {
	struct position pos;
	struct annotation_token *tokens;
	struct member_ref *lock;
	struct member_ref *data;
	bool parsed;
	bool processed;
	bool resolved;
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
		copy->type = token_type(token);
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

static bool
token_is(const struct annotation_token *token, const char *text)
{
	return (token != NULL && strcmp(token->text, text) == 0);
}

static bool
annotation_error(struct annotation *annotation, struct annotation_token *token,
    const char *message)
{
	sparse_error(token != NULL ? token->pos : annotation->pos,
	    "locklint: %s", message);
	return (false);
}

static struct member_ref *
alloc_member_ref(struct annotation_token *type,
    struct annotation_token *member)
{
	struct member_ref *ref;

	ref = calloc(1, sizeof (*ref));
	if (ref == NULL)
		die("out of memory parsing locklint annotation");
	ref->pos = member->pos;
	ref->type_name = copy_string(type->text);
	ref->member_name = copy_string(member->text);
	return (ref);
}

static void
add_member_ref(struct member_ref **head, struct member_ref ***tail,
    struct member_ref *ref)
{
	if (*tail == NULL)
		*tail = head;
	**tail = ref;
	*tail = &ref->next;
}

static bool
parse_member_refs(struct annotation *annotation,
    struct annotation_token **cursor, struct member_ref **head)
{
	struct annotation_token *type = *cursor;
	struct member_ref **tail = NULL;

	if (type == NULL || type->type != TOKEN_IDENT)
		return (annotation_error(annotation, type,
		    "expected annotation type name"));
	*cursor = type->next;
	if (token_is(*cursor, "::")) {
		*cursor = (*cursor)->next;
	} else if (token_is(*cursor, ":") &&
	    token_is((*cursor)->next, ":")) {
		*cursor = (*cursor)->next->next;
	} else {
		return (annotation_error(annotation, *cursor,
		    "expected '::' after annotation type"));
	}

	if (!token_is(*cursor, "{")) {
		struct annotation_token *member = *cursor;

		if (member == NULL || member->type != TOKEN_IDENT)
			return (annotation_error(annotation, member,
			    "expected annotation member name"));
		add_member_ref(head, &tail, alloc_member_ref(type, member));
		*cursor = member->next;
		return (true);
	}

	*cursor = (*cursor)->next;
	while (*cursor != NULL && !token_is(*cursor, "}")) {
		struct annotation_token *member = *cursor;

		if (token_is(member, ",")) {
			*cursor = member->next;
			continue;
		}
		if (member->type != TOKEN_IDENT)
			return (annotation_error(annotation, member,
			    "expected member name in annotation brace list"));
		add_member_ref(head, &tail, alloc_member_ref(type, member));
		*cursor = member->next;
	}
	if (!token_is(*cursor, "}"))
		return (annotation_error(annotation, *cursor,
		    "expected '}' after annotation member list"));
	if (*head == NULL)
		return (annotation_error(annotation, *cursor,
		    "empty annotation member list"));
	*cursor = (*cursor)->next;
	return (true);
}

static bool
parse_annotation(struct annotation *annotation)
{
	struct annotation_token *cursor = annotation->tokens;
	struct member_ref *lock = NULL;

	if (!token_is(cursor, "MUTEX_PROTECTS_DATA"))
		return (false);
	cursor = cursor->next;
	if (!token_is(cursor, "("))
		return (annotation_error(annotation, cursor,
		    "expected '(' after MUTEX_PROTECTS_DATA"));
	cursor = cursor->next;
	if (!parse_member_refs(annotation, &cursor, &lock))
		return (false);
	if (lock->next != NULL)
		return (annotation_error(annotation, cursor,
		    "MUTEX_PROTECTS_DATA requires one lock member"));
	if (!token_is(cursor, ","))
		return (annotation_error(annotation, cursor,
		    "expected ',' after protected-data lock"));
	cursor = cursor->next;
	if (!parse_member_refs(annotation, &cursor, &annotation->data))
		return (false);
	if (!token_is(cursor, ")"))
		return (annotation_error(annotation, cursor,
		    "expected ')' after MUTEX_PROTECTS_DATA"));
	if (cursor->next != NULL)
		return (annotation_error(annotation, cursor->next,
		    "unexpected tokens after MUTEX_PROTECTS_DATA"));

	annotation->lock = lock;
	annotation->parsed = true;
	return (true);
}

static struct symbol *
resolve_type(const char *name)
{
	struct ident *ident;
	struct symbol *type;

	ident = built_in_ident(name);
	type = lookup_symbol(ident, NS_TYPEDEF);
	if (type == NULL)
		type = lookup_symbol(ident, NS_STRUCT);
	while (type != NULL && type->type == SYM_NODE)
		type = get_base_type(type);
	if (type == NULL ||
	    (type->type != SYM_STRUCT && type->type != SYM_UNION))
		return (NULL);
	examine_symbol_type(type);
	return (type);
}

static bool
resolve_member_ref(struct member_ref *ref)
{
	struct ident *ident;

	ref->type = resolve_type(ref->type_name);
	if (ref->type == NULL) {
		sparse_error(ref->pos,
		    "locklint: unresolved annotation type '%s'",
		    ref->type_name);
		return (false);
	}
	ident = built_in_ident(ref->member_name);
	ref->offset = 0;
	ref->member = find_identifier(ident, ref->type->symbol_list,
	    &ref->offset);
	if (ref->member == NULL) {
		sparse_error(ref->pos,
		    "locklint: unresolved annotation member '%s::%s'",
		    ref->type_name, ref->member_name);
		return (false);
	}
	return (true);
}

void
locklint_resolve_annotations(void)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct member_ref *ref;

		if (annotation->processed)
			continue;
		annotation->processed = true;
		if (!parse_annotation(annotation))
			continue;
		if (!resolve_member_ref(annotation->lock))
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			if (!resolve_member_ref(ref))
				break;
		}
		if (ref == NULL)
			annotation->resolved = true;
	}
}

struct symbol *
locklint_protecting_member(struct symbol *member)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct member_ref *ref;

		if (!annotation->resolved)
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			if (ref->member == member)
				return (annotation->lock->member);
		}
	}
	return (NULL);
}

static void
show_raw_annotation(FILE *stream, struct annotation *annotation)
{
	struct annotation_token *token;
	bool first = true;

	(void) fputs("_NOTE(", stream);
	for (token = annotation->tokens; token != NULL; token = token->next) {
		if (!first && (token->pos.whitespace || token->pos.newline))
			(void) fputc(' ', stream);
		(void) fputs(token->text, stream);
		first = false;
	}
	(void) fputc(')', stream);
}

void
locklint_show_annotations(FILE *stream)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct member_ref *ref;

		if (!annotation->parsed) {
			(void) fprintf(stream, "%s:%u:%u: ",
			    stream_name(annotation->pos.stream),
			    annotation->pos.line, annotation->pos.pos);
			show_raw_annotation(stream, annotation);
			(void) fputc('\n', stream);
			continue;
		}
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			(void) fprintf(stream,
			    "%s:%u:%u: MUTEX_PROTECTS_DATA "
			    "%s::%s -> %s::%s\n",
			    stream_name(annotation->pos.stream),
			    annotation->pos.line, annotation->pos.pos,
			    annotation->lock->type_name,
			    annotation->lock->member_name,
			    ref->type_name, ref->member_name);
		}
	}
}
