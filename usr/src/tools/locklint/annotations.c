/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Capture, parse, and resolve source-level locking annotations.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "access.h"
#include "annotations.h"
#include "expression.h"
#include "identity.h"
#include "linearize.h"
#include "symbol.h"
#include "token.h"

struct annotation_token {
	struct position pos;
	enum token_type type;
	char *text;
	struct annotation_token *next;
};

enum annotation_scope {
	ANNOTATION_AUTO,
	ANNOTATION_TYPE,
	ANNOTATION_OBJECT
};

enum annotation_kind {
	ANNOTATION_UNSUPPORTED,
	ANNOTATION_MUTEX_PROTECTS_DATA,
	ANNOTATION_RWLOCK_PROTECTS_DATA,
	ANNOTATION_SCHEME_PROTECTS_DATA,
	ANNOTATION_DATA_READABLE_WITHOUT_LOCK,
	ANNOTATION_READ_ONLY_DATA,
	ANNOTATION_LOCK_ORDER
};

struct annotation_ref {
	struct position pos;
	enum annotation_scope scope;
	char *base_name;
	char *path;
	struct symbol *root;
	struct object_identity *object;
	struct symbol *owner_type;
	struct symbol *type;
	struct symbol *member;
	unsigned long offset;
	struct annotation_ref *replaces;
	struct annotation_ref *replaced_by;
	struct position annotation_pos;
	struct annotation_ref *next;
};

struct annotation {
	struct position pos;
	struct translation_unit *tu;
	struct annotation_token *tokens;
	struct annotation_ref *lock;
	struct annotation_ref *data;
	struct annotation_ref *order;
	const char *scheme;
	enum annotation_kind kind;
	bool parsed;
	bool processed;
	bool resolved;
	struct annotation *next;
};

static struct annotation *annotations;
static struct annotation **annotations_tail = &annotations;

static enum locklint_execution_kind execution_kind(const struct token *);

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

/*
 * Preserve the unexpanded _NOTE body and its translation-unit provenance
 * before Sparse releases the preprocessing tokens.
 */
static int
capture_annotation(const struct token *macro,
    const struct token *open, void *data)
{
	struct annotation *annotation;
	struct annotation_token **tail;
	const struct token *end;
	const struct token *token;

	(void) data;

	if (execution_kind(open) != LOCKLINT_EXECUTION_NONE)
		return (1);

	end = annotation_end(open);
	if (end == NULL)
		return (0);

	annotation = calloc(1, sizeof (*annotation));
	if (annotation == NULL)
		die("out of memory recording locklint annotation");
	annotation->pos = macro->pos;
	/* The same header position can be captured in several translations. */
	annotation->tu = locklint_translation_unit_current();
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
	return (0);
}

static enum locklint_execution_kind
execution_kind(const struct token *open)
{
	const struct token *name = open->next;
	const char *text;

	if (name == NULL || token_type(name) != TOKEN_IDENT)
		return (LOCKLINT_EXECUTION_NONE);
	text = show_token(name);
	if (strcmp(text, "NO_COMPETING_THREADS") == 0)
		return (LOCKLINT_EXECUTION_ASSERT_NO_COMPETITION);
	if (strcmp(text, "NO_COMPETING_THREADS_NOW") == 0)
		return (LOCKLINT_EXECUTION_NO_COMPETITION);
	if (strcmp(text, "COMPETING_THREADS_NOW") == 0)
		return (LOCKLINT_EXECUTION_COMPETITION);
	if (strcmp(text, "NOW_INVISIBLE_TO_OTHER_THREADS") == 0)
		return (LOCKLINT_EXECUTION_INVISIBLE);
	if (strcmp(text, "NOW_VISIBLE_TO_OTHER_THREADS") == 0)
		return (LOCKLINT_EXECUTION_VISIBLE);
	if (strcmp(text, "ASSUMING_PROTECTED") == 0)
		return (LOCKLINT_EXECUTION_ASSUME_PROTECTED);
	if (strcmp(text, "NO_COMPETING_THREADS_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_NO_COMPETITION_EFFECT);
	if (strcmp(text, "COMPETING_THREADS_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_COMPETITION_EFFECT);
	if (strcmp(text, "MUTEX_ACQUIRED_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_MUTEX_ACQUIRED_EFFECT);
	if (strcmp(text, "READ_LOCK_ACQUIRED_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_READ_ACQUIRED_EFFECT);
	if (strcmp(text, "WRITE_LOCK_ACQUIRED_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_WRITE_ACQUIRED_EFFECT);
	if (strcmp(text, "LOCK_RELEASED_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_LOCK_RELEASED_EFFECT);
	return (LOCKLINT_EXECUTION_NONE);
}

void
locklint_annotations_enable(void)
{
	add_macro_expansion_hook("_NOTE", capture_annotation, NULL);
	add_pre_buffer("#define NO_COMPETING_THREADS_NOW "
	    "__context__(0, 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_NO_COMPETITION);
	add_pre_buffer("#define COMPETING_THREADS_NOW "
	    "__context__(0, 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_COMPETITION);
	add_pre_buffer("#define NO_COMPETING_THREADS_AS_SIDE_EFFECT "
	    "__context__(0, 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_NO_COMPETITION_EFFECT);
	add_pre_buffer("#define COMPETING_THREADS_AS_SIDE_EFFECT "
	    "__context__(0, 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_COMPETITION_EFFECT);
	add_pre_buffer("#define MUTEX_ACQUIRED_AS_SIDE_EFFECT(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_MUTEX_ACQUIRED_EFFECT);
	add_pre_buffer("#define READ_LOCK_ACQUIRED_AS_SIDE_EFFECT(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_READ_ACQUIRED_EFFECT);
	add_pre_buffer("#define WRITE_LOCK_ACQUIRED_AS_SIDE_EFFECT(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_WRITE_ACQUIRED_EFFECT);
	add_pre_buffer("#define LOCK_RELEASED_AS_SIDE_EFFECT(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_LOCK_RELEASED_EFFECT);
	add_pre_buffer("#define NOW_INVISIBLE_TO_OTHER_THREADS(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_INVISIBLE);
	add_pre_buffer("#define NOW_VISIBLE_TO_OTHER_THREADS(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_VISIBLE);
	add_pre_buffer("#define ASSUMING_PROTECTED(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_ASSUME_PROTECTED);
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

static bool
annotation_named_error(struct annotation *annotation,
    struct annotation_token *token, const char *message, const char *name)
{
	sparse_error(token != NULL ? token->pos : annotation->pos,
	    "locklint: %s %s", message, name);
	return (false);
}

static char *
join_path(const char *prefix, const char *name)
{
	char *path;
	size_t prefix_len = prefix != NULL ? strlen(prefix) : 0;
	size_t name_len = strlen(name);

	path = malloc(prefix_len + (prefix_len != 0 ? 1 : 0) + name_len + 1);
	if (path == NULL)
		die("out of memory parsing locklint annotation path");
	if (prefix_len != 0) {
		(void) memcpy(path, prefix, prefix_len);
		path[prefix_len] = '.';
		(void) memcpy(path + prefix_len + 1, name, name_len + 1);
	} else {
		(void) memcpy(path, name, name_len + 1);
	}
	return (path);
}

static struct annotation_ref *
alloc_annotation_ref(struct annotation_token *base,
    enum annotation_scope scope, const char *path)
{
	struct annotation_ref *ref;

	ref = calloc(1, sizeof (*ref));
	if (ref == NULL)
		die("out of memory parsing locklint annotation");
	ref->pos = base->pos;
	ref->scope = scope;
	ref->base_name = copy_string(base->text);
	if (path != NULL)
		ref->path = copy_string(path);
	return (ref);
}

static void
add_annotation_ref(struct annotation_ref **head,
    struct annotation_ref ***tail, struct annotation_ref *ref)
{
	if (*tail == NULL)
		*tail = head;
	**tail = ref;
	*tail = &ref->next;
}

static bool parse_path(struct annotation *, struct annotation_token **,
    struct annotation_token *, enum annotation_scope, const char *,
    struct annotation_ref **, struct annotation_ref ***);

static bool
parse_path_group(struct annotation *annotation,
    struct annotation_token **cursor, struct annotation_token *base,
    enum annotation_scope scope, const char *prefix,
    struct annotation_ref **head, struct annotation_ref ***tail)
{
	bool any = false;

	if (!token_is(*cursor, "{"))
		return (annotation_error(annotation, *cursor,
		    "expected '{' in annotation name generator"));
	*cursor = (*cursor)->next;
	while (*cursor != NULL && !token_is(*cursor, "}")) {
		if (token_is(*cursor, ",")) {
			*cursor = (*cursor)->next;
			continue;
		}
		if (!parse_path(annotation, cursor, base, scope, prefix,
		    head, tail))
			return (false);
		any = true;
	}
	if (!token_is(*cursor, "}"))
		return (annotation_error(annotation, *cursor,
		    "expected '}' after annotation name generator"));
	if (!any)
		return (annotation_error(annotation, *cursor,
		    "empty annotation name generator"));
	*cursor = (*cursor)->next;
	return (true);
}

static bool
parse_path(struct annotation *annotation, struct annotation_token **cursor,
    struct annotation_token *base, enum annotation_scope scope,
    const char *prefix, struct annotation_ref **head,
    struct annotation_ref ***tail)
{
	struct annotation_token *component = *cursor;
	char *path;

	if (component == NULL || component->type != TOKEN_IDENT)
		return (annotation_error(annotation, component,
		    "expected annotation name component"));
	path = join_path(prefix, component->text);
	*cursor = component->next;
	if (token_is(*cursor, ".")) {
		*cursor = (*cursor)->next;
		if (token_is(*cursor, "{")) {
			bool parsed;

			parsed = parse_path_group(annotation, cursor, base, scope,
			    path, head, tail);
			free(path);
			return (parsed);
		}
		if (!parse_path(annotation, cursor, base, scope, path, head,
		    tail)) {
			free(path);
			return (false);
		}
		free(path);
		return (true);
	}
	add_annotation_ref(head, tail,
	    alloc_annotation_ref(base, scope, path));
	free(path);
	return (true);
}

static bool
consume_type_separator(struct annotation_token **cursor)
{
	if (token_is(*cursor, "::")) {
		*cursor = (*cursor)->next;
		return (true);
	}
	if (token_is(*cursor, ":") && token_is((*cursor)->next, ":")) {
		*cursor = (*cursor)->next->next;
		return (true);
	}
	return (false);
}

static bool
parse_name(struct annotation *annotation, struct annotation_token **cursor,
    struct annotation_ref **head, struct annotation_ref ***tail)
{
	struct annotation_token *base = *cursor;
	enum annotation_scope scope;

	if (base == NULL || base->type != TOKEN_IDENT)
		return (annotation_error(annotation, base,
		    "expected annotation name"));
	*cursor = base->next;
	if (consume_type_separator(cursor)) {
		scope = ANNOTATION_TYPE;
		if (token_is(*cursor, "{"))
			return (parse_path_group(annotation, cursor, base, scope,
			    NULL, head, tail));
		return (parse_path(annotation, cursor, base, scope, NULL,
		    head, tail));
	}
	if (token_is(*cursor, ".")) {
		scope = ANNOTATION_OBJECT;
		*cursor = (*cursor)->next;
		return (parse_path(annotation, cursor, base, scope, NULL,
		    head, tail));
	}
	add_annotation_ref(head, tail,
	    alloc_annotation_ref(base, ANNOTATION_AUTO, NULL));
	return (true);
}

static bool
parse_name_list(struct annotation *annotation,
    struct annotation_token **cursor, struct annotation_ref **head,
    const char *end)
{
	struct annotation_ref **tail = NULL;

	while (*cursor != NULL && !token_is(*cursor, end)) {
		if (token_is(*cursor, ",")) {
			*cursor = (*cursor)->next;
			continue;
		}
		if (!parse_name(annotation, cursor, head, &tail))
			return (false);
	}
	if (*head == NULL)
		return (annotation_error(annotation, *cursor,
		    "empty annotation name list"));
	return (true);
}

static bool
finish_data_annotation(struct annotation *annotation,
    struct annotation_token *cursor, const char *name)
{
	if (!parse_name_list(annotation, &cursor, &annotation->data, ")"))
		return (false);
	if (!token_is(cursor, ")"))
		return (annotation_named_error(annotation, cursor,
		    "expected ')' after", name));
	if (cursor->next != NULL)
		return (annotation_named_error(annotation, cursor->next,
		    "unexpected tokens after", name));
	annotation->parsed = true;
	return (true);
}

static bool
finish_order_annotation(struct annotation *annotation,
    struct annotation_token *cursor, const char *name)
{
	struct annotation_ref **tail = NULL;
	struct annotation_ref *ref;
	unsigned int count = 0;
	bool after_comma = false;

	while (cursor != NULL && !token_is(cursor, ")")) {
		if (token_is(cursor, ",")) {
			if (annotation->order == NULL || after_comma) {
				return (annotation_error(annotation, cursor,
				    "unexpected comma in LOCK_ORDER"));
			}
			after_comma = true;
			cursor = cursor->next;
			continue;
		}
		if (!parse_name(annotation, &cursor, &annotation->order, &tail))
			return (false);
		after_comma = false;
	}
	if (after_comma) {
		return (annotation_error(annotation, cursor,
		    "trailing comma in LOCK_ORDER"));
	}
	for (ref = annotation->order; ref != NULL; ref = ref->next)
		count++;
	if (count < 2) {
		return (annotation_error(annotation, cursor,
		    "LOCK_ORDER requires at least two lock names"));
	}
	if (!token_is(cursor, ")"))
		return (annotation_named_error(annotation, cursor,
		    "expected ')' after", name));
	if (cursor->next != NULL)
		return (annotation_named_error(annotation, cursor->next,
		    "unexpected tokens after", name));
	annotation->parsed = true;
	return (true);
}

static bool
parse_annotation(struct annotation *annotation)
{
	struct annotation_token *cursor = annotation->tokens;
	struct annotation_ref *lock = NULL;
	struct annotation_ref **tail = NULL;
	const char *name;

	if (token_is(cursor, "MUTEX_PROTECTS_DATA")) {
		annotation->kind = ANNOTATION_MUTEX_PROTECTS_DATA;
	} else if (token_is(cursor, "RWLOCK_PROTECTS_DATA")) {
		annotation->kind = ANNOTATION_RWLOCK_PROTECTS_DATA;
	} else if (token_is(cursor, "SCHEME_PROTECTS_DATA")) {
		annotation->kind = ANNOTATION_SCHEME_PROTECTS_DATA;
	} else if (token_is(cursor, "DATA_READABLE_WITHOUT_LOCK")) {
		annotation->kind = ANNOTATION_DATA_READABLE_WITHOUT_LOCK;
	} else if (token_is(cursor, "READ_ONLY_DATA")) {
		annotation->kind = ANNOTATION_READ_ONLY_DATA;
	} else if (token_is(cursor, "LOCK_ORDER")) {
		annotation->kind = ANNOTATION_LOCK_ORDER;
	} else {
		return (false);
	}
	name = cursor->text;
	cursor = cursor->next;
	if (!token_is(cursor, "("))
		return (annotation_named_error(annotation, cursor,
		    "expected '(' after", name));
	cursor = cursor->next;
	switch (annotation->kind) {
	case ANNOTATION_MUTEX_PROTECTS_DATA:
	case ANNOTATION_RWLOCK_PROTECTS_DATA:
		if (!parse_name(annotation, &cursor, &lock, &tail))
			return (false);
		if (lock->next != NULL) {
			return (annotation_error(annotation, cursor,
			    annotation->kind ==
			    ANNOTATION_MUTEX_PROTECTS_DATA ?
			    "MUTEX_PROTECTS_DATA requires one lock name" :
			    "RWLOCK_PROTECTS_DATA requires one lock name"));
		}
		if (!token_is(cursor, ","))
			return (annotation_error(annotation, cursor,
			    "expected ',' after protected-data lock"));
		annotation->lock = lock;
		cursor = cursor->next;
		break;
	case ANNOTATION_SCHEME_PROTECTS_DATA:
		if (cursor == NULL || cursor->type != TOKEN_STRING)
			return (annotation_error(annotation, cursor,
			    "expected quoted protection scheme"));
		annotation->scheme = cursor->text;
		cursor = cursor->next;
		if (!token_is(cursor, ","))
			return (annotation_error(annotation, cursor,
			    "expected ',' after protection scheme"));
		cursor = cursor->next;
		break;
	case ANNOTATION_DATA_READABLE_WITHOUT_LOCK:
	case ANNOTATION_READ_ONLY_DATA:
		break;
	case ANNOTATION_LOCK_ORDER:
		return (finish_order_annotation(annotation, cursor, name));
	default:
		abort();
	}
	return (finish_data_annotation(annotation, cursor, name));
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

static struct symbol *
strip_node_type(struct symbol *type)
{
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type);
}

static struct symbol *
direct_compound_type(struct symbol *type)
{
	type = strip_node_type(type);
	while (type != NULL && type->type == SYM_ARRAY)
		type = strip_node_type(type->ctype.base_type);
	if (type == NULL ||
	    (type->type != SYM_STRUCT && type->type != SYM_UNION))
		return (NULL);
	examine_symbol_type(type);
	return (type);
}

static char *
annotation_ref_name(const struct annotation_ref *ref)
{
	const char *separator;
	char *name;
	size_t base_len = strlen(ref->base_name);
	size_t path_len = ref->path != NULL ? strlen(ref->path) : 0;
	size_t separator_len;

	if (ref->path == NULL)
		return (copy_string(ref->base_name));
	separator = ref->scope == ANNOTATION_TYPE ? "::" : ".";
	separator_len = strlen(separator);
	name = malloc(base_len + separator_len + path_len + 1);
	if (name == NULL)
		die("out of memory formatting locklint annotation name");
	(void) memcpy(name, ref->base_name, base_len);
	(void) memcpy(name + base_len, separator, separator_len);
	(void) memcpy(name + base_len + separator_len, ref->path,
	    path_len + 1);
	return (name);
}

static bool
resolve_path(struct annotation_ref *ref, struct symbol *type)
{
	const char *component = ref->path;
	const char *end;

	while (component != NULL && *component != '\0') {
		struct ident *ident;
		struct symbol *member;
		char *name;
		size_t length;
		int offset = 0;

		type = direct_compound_type(type);
		if (type == NULL)
			break;
		end = strchr(component, '.');
		length = end != NULL ? (size_t)(end - component) :
		    strlen(component);
		name = malloc(length + 1);
		if (name == NULL)
			die("out of memory resolving locklint annotation");
		(void) memcpy(name, component, length);
		name[length] = '\0';
		ident = built_in_ident(name);
		free(name);
		member = find_identifier(ident, type->symbol_list, &offset);
		if (member == NULL)
			break;
		ref->member = member;
		ref->offset += offset;
		type = member->ctype.base_type;
		component = end != NULL ? end + 1 : NULL;
	}
	if (component != NULL) {
		char *name = annotation_ref_name(ref);

		sparse_error(ref->pos,
		    "locklint: unresolved annotation name '%s'", name);
		free(name);
		return (false);
	}
	ref->type = type;
	return (true);
}

/*
 * Resolve automatic names in the current translation unit, preferring an
 * object over a type, and retain canonical identity only for object scope.
 */
static bool
resolve_annotation_ref(struct annotation_ref *ref, bool lock,
    struct translation_unit *tu)
{
	struct ident *ident = built_in_ident(ref->base_name);
	struct symbol *base;

	if (ref->scope == ANNOTATION_TYPE) {
		base = resolve_type(ref->base_name);
	} else {
		base = lookup_symbol(ident, NS_SYMBOL);
		if (base == NULL && ref->scope == ANNOTATION_AUTO) {
			base = resolve_type(ref->base_name);
			if (base != NULL)
				ref->scope = ANNOTATION_TYPE;
		}
	}
	if (base == NULL) {
		char *name = annotation_ref_name(ref);

		sparse_error(ref->pos,
		    "locklint: unresolved annotation name '%s'", name);
		free(name);
		return (false);
	}

	if (ref->scope != ANNOTATION_TYPE) {
		ref->scope = ANNOTATION_OBJECT;
		ref->root = base;
		/* Object annotations may denote one object across translations. */
		ref->object = locklint_object_identity(tu, base);
		ref->type = base->ctype.base_type;
	} else {
		/* C tag names have no linker identity; keep Sparse type identity. */
		ref->type = base;
	}
	ref->owner_type = direct_compound_type(ref->type);
	if (ref->path != NULL && !resolve_path(ref, ref->type))
		return (false);
	if (lock && ref->scope == ANNOTATION_TYPE && ref->member == NULL &&
	    direct_compound_type(ref->type) != NULL) {
		char *name = annotation_ref_name(ref);

		sparse_error(ref->pos,
		    "locklint: annotation lock '%s' names a structure", name);
		free(name);
		return (false);
	}
	return (true);
}

static struct annotation_ref *
clone_expanded_ref(const struct annotation_ref *source,
    struct symbol *member, const char *path)
{
	struct annotation_ref *ref;

	ref = calloc(1, sizeof (*ref));
	if (ref == NULL)
		die("out of memory expanding locklint annotation");
	ref->pos = source->pos;
	ref->scope = source->scope;
	ref->base_name = copy_string(source->base_name);
	if (path != NULL)
		ref->path = copy_string(path);
	ref->root = source->root;
	ref->object = source->object;
	ref->owner_type = source->owner_type;
	ref->type = member->ctype.base_type;
	ref->member = member;
	ref->offset = source->offset + member->offset;
	return (ref);
}

/*
 * Replace an aggregate data reference with its named leaf members.  Omit the
 * protecting lock when it belongs to the same annotated object or type.
 */
static void
expand_compound_ref(struct annotation_ref *source, struct symbol *type,
    const struct annotation_ref *lock, struct annotation_ref **head,
    struct annotation_ref ***tail)
{
	struct symbol *member;

	type = direct_compound_type(type);
	if (type == NULL) {
		add_annotation_ref(head, tail, source);
		return;
	}

	FOR_EACH_PTR(type->symbol_list, member) {
		struct annotation_ref *expanded;
		struct symbol *member_type;
		char *path;

		if (member->ident == NULL) {
			member_type = direct_compound_type(
			    member->ctype.base_type);
			if (member_type != NULL) {
				expanded = clone_expanded_ref(source, member,
				    source->path);
				expand_compound_ref(expanded, member_type, lock,
				    head, tail);
			}
			continue;
		}
		if (lock != NULL && member == lock->member) {
			struct locklint_access lock_access = { 0 };
			struct locklint_access source_access = { 0 };

			/*
			 * A type-scoped lock has no root.  Object-scoped locks
			 * use canonical identity when available and Sparse root
			 * identity otherwise.
			 */
			lock_access.root = lock->root;
			lock_access.object = lock->object;
			source_access.root = source->root;
			source_access.object = source->object;
			if (lock->root == NULL ||
			    locklint_same_access(&lock_access, &source_access))
				continue;
		}
		path = join_path(source->path, show_ident(member->ident));
		expanded = clone_expanded_ref(source, member, path);
		free(path);
		member_type = direct_compound_type(member->ctype.base_type);
		if (member_type != NULL) {
			expand_compound_ref(expanded, member_type, lock, head,
			    tail);
		} else {
			add_annotation_ref(head, tail, expanded);
		}
	} END_FOR_EACH_PTR(member);
}

static void
expand_data_refs(struct annotation *annotation)
{
	struct annotation_ref *expanded = NULL;
	struct annotation_ref **tail = NULL;
	struct annotation_ref *ref;

	for (ref = annotation->data; ref != NULL; ) {
		struct annotation_ref *next = ref->next;
		struct symbol *type = direct_compound_type(ref->type);

		ref->next = NULL;
		if (type != NULL)
			expand_compound_ref(ref, type, annotation->lock,
			    &expanded, &tail);
		else
			add_annotation_ref(&expanded, &tail, ref);
		ref = next;
	}
	annotation->data = expanded;
}

static bool
same_data_ref(const struct annotation_ref *left,
    const struct annotation_ref *right)
{
	/*
	 * Object annotations compare canonical roots across translation units.
	 * Type annotations remain local to their separately parsed Sparse type.
	 */
	if (left->root != NULL || right->root != NULL) {
		struct locklint_access left_access = { 0 };
		struct locklint_access right_access = { 0 };

		if (left->root == NULL || right->root == NULL)
			return (false);
		left_access.root = left->root;
		left_access.object = left->object;
		left_access.member = left->member;
		left_access.offset = left->offset;
		right_access.root = right->root;
		right_access.object = right->object;
		right_access.member = right->member;
		right_access.offset = right->offset;
		return (locklint_same_access(&left_access, &right_access));
	}
	return (left->owner_type == right->owner_type &&
	    left->member == right->member && left->offset == right->offset);
}

/*
 * For each newly resolved datum, link the latest earlier declaration of that
 * same datum so dumps can show last-declaration-wins provenance.
 */
static void
record_replacements(struct annotation *annotation)
{
	struct annotation_ref *ref;

	for (ref = annotation->data; ref != NULL; ref = ref->next) {
		struct annotation *earlier;
		struct annotation_ref *previous = NULL;

		for (earlier = annotations; earlier != annotation;
		    earlier = earlier->next) {
			struct annotation_ref *candidate;

			if (!earlier->resolved ||
			    (earlier->kind != ANNOTATION_MUTEX_PROTECTS_DATA &&
			    earlier->kind != ANNOTATION_RWLOCK_PROTECTS_DATA &&
			    earlier->kind != ANNOTATION_SCHEME_PROTECTS_DATA))
				continue;
			for (candidate = earlier->data; candidate != NULL;
			    candidate = candidate->next) {
				if (same_data_ref(candidate, ref))
					previous = candidate;
			}
		}
		if (previous != NULL) {
			previous->replaced_by = ref;
			ref->replaces = previous;
		}
		ref->annotation_pos = annotation->pos;
	}
}

/*
 * Resolve each annotation exactly once while the namespace of its defining
 * translation unit is still current.
 */
void
locklint_resolve_annotations(void)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_ref *ref;

		if (annotation->processed)
			continue;
		annotation->processed = true;
		if (!parse_annotation(annotation))
			continue;
		if (annotation->kind == ANNOTATION_LOCK_ORDER) {
			for (ref = annotation->order; ref != NULL;
			    ref = ref->next) {
				if (!resolve_annotation_ref(ref, true,
				    annotation->tu))
					break;
			}
			if (ref == NULL)
				annotation->resolved = true;
			continue;
		}
		if (annotation->lock != NULL &&
		    !resolve_annotation_ref(annotation->lock, true,
		    annotation->tu))
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			if (!resolve_annotation_ref(ref, false, annotation->tu))
				break;
		}
		if (ref == NULL) {
			expand_data_refs(annotation);
			if (annotation->kind ==
			    ANNOTATION_MUTEX_PROTECTS_DATA ||
			    annotation->kind ==
			    ANNOTATION_RWLOCK_PROTECTS_DATA ||
			    annotation->kind ==
			    ANNOTATION_SCHEME_PROTECTS_DATA)
				record_replacements(annotation);
			annotation->resolved = true;
		}
	}
}

enum locklint_execution_kind
locklint_get_execution_annotation(const struct instruction *insn)
{
	if (insn->opcode != OP_CONTEXT || insn->increment != 0)
		return (LOCKLINT_EXECUTION_NONE);

	switch (insn->context_tag) {
	case LOCKLINT_EXECUTION_NO_COMPETITION:
	case LOCKLINT_EXECUTION_COMPETITION:
	case LOCKLINT_EXECUTION_INVISIBLE:
	case LOCKLINT_EXECUTION_VISIBLE:
	case LOCKLINT_EXECUTION_ASSUME_PROTECTED:
	case LOCKLINT_EXECUTION_NO_COMPETITION_EFFECT:
	case LOCKLINT_EXECUTION_COMPETITION_EFFECT:
	case LOCKLINT_EXECUTION_MUTEX_ACQUIRED_EFFECT:
	case LOCKLINT_EXECUTION_READ_ACQUIRED_EFFECT:
	case LOCKLINT_EXECUTION_WRITE_ACQUIRED_EFFECT:
	case LOCKLINT_EXECUTION_LOCK_RELEASED_EFFECT:
	case LOCKLINT_EXECUTION_ASSERT_NO_COMPETITION:
		return ((enum locklint_execution_kind)insn->context_tag);
	default:
		return (LOCKLINT_EXECUTION_NONE);
	}
}

static const char *
declared_lock_effect_name(enum locklint_declared_lock_effect effect)
{
	switch (effect) {
	case LOCKLINT_DECLARED_MUTEX_ACQUIRED:
		return ("MUTEX_ACQUIRED_AS_SIDE_EFFECT");
	case LOCKLINT_DECLARED_READ_ACQUIRED:
		return ("READ_LOCK_ACQUIRED_AS_SIDE_EFFECT");
	case LOCKLINT_DECLARED_WRITE_ACQUIRED:
		return ("WRITE_LOCK_ACQUIRED_AS_SIDE_EFFECT");
	case LOCKLINT_DECLARED_LOCK_RELEASED:
		return ("LOCK_RELEASED_AS_SIDE_EFFECT");
	default:
		abort();
	}
}

bool
locklint_get_declared_lock_effect(struct translation_unit *tu,
    const struct instruction *insn, enum locklint_declared_lock_effect *effect,
    struct locklint_access *lock)
{
	*effect = LOCKLINT_DECLARED_LOCK_NONE;
	*lock = (struct locklint_access){ 0 };

	switch (locklint_get_execution_annotation(insn)) {
	case LOCKLINT_EXECUTION_MUTEX_ACQUIRED_EFFECT:
		*effect = LOCKLINT_DECLARED_MUTEX_ACQUIRED;
		break;
	case LOCKLINT_EXECUTION_READ_ACQUIRED_EFFECT:
		*effect = LOCKLINT_DECLARED_READ_ACQUIRED;
		break;
	case LOCKLINT_EXECUTION_WRITE_ACQUIRED_EFFECT:
		*effect = LOCKLINT_DECLARED_WRITE_ACQUIRED;
		break;
	case LOCKLINT_EXECUTION_LOCK_RELEASED_EFFECT:
		*effect = LOCKLINT_DECLARED_LOCK_RELEASED;
		break;
	default:
		return (false);
	}

	return (insn->context_expr != NULL &&
	    locklint_get_access(tu, insn->context_expr, lock));
}

void
locklint_process_function_annotations(FILE *stream,
    struct translation_unit *tu, struct entrypoint *ep)
{
	struct basic_block *bb;
	char function[128];

	(void) snprintf(function, sizeof (function), "%s",
	    ep->name->ident != NULL ? show_ident(ep->name->ident) :
	    "<anonymous>");
	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct locklint_access lock;
			enum locklint_declared_lock_effect effect;
			struct position pos;
			bool resolved;

			if (insn->bb == NULL)
				continue;
			resolved = locklint_get_declared_lock_effect(tu, insn,
			    &effect, &lock);
			if (effect == LOCKLINT_DECLARED_LOCK_NONE)
				continue;
			if (!resolved) {
				pos = insn->context_expr != NULL ?
				    insn->context_expr->pos : insn->pos;
				sparse_error(pos,
				    "locklint: %s requires a lock expression",
				    declared_lock_effect_name(effect));
				continue;
			}
			if (stream == NULL)
				continue;
			pos = insn->context_expr->pos;
			(void) fprintf(stream, "%s:%u:%u: %s ",
			    stream_name(pos.stream), pos.line, pos.pos,
			    declared_lock_effect_name(effect));
			locklint_show_access(stream, insn->context_expr);
			(void) fprintf(stream, " function=%s\n", function);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static bool
matching_data_ref(const struct annotation_ref *ref,
    const struct locklint_access *access, unsigned long *base)
{
	if (ref->root != NULL) {
		struct locklint_access target = { 0 };

		target.root = ref->root;
		target.object = ref->object;
		target.member = ref->member;
		target.offset = ref->offset;
		if (!locklint_same_access(&target, access))
			return (false);
		*base = 0;
		return (true);
	}
	if (ref->member != access->member ||
	    !locklint_access_base(access, ref->owner_type, ref->offset, base))
		return (false);
	return (true);
}

/*
 * Combine all matching policy dimensions.  The last mechanical or scheme
 * declaration wins; unlocked-read and read-only properties are additive.
 */
bool
locklint_data_policy(const struct locklint_access *access,
    struct locklint_data_policy *policy, struct locklint_access *lock)
{
	struct annotation *annotation;
	const struct annotation_ref *protector = NULL;
	unsigned long protector_base = 0;
	unsigned long protected_offset = 0;
	bool found = false;

	(void) memset(policy, 0, sizeof (*policy));
	(void) memset(lock, 0, sizeof (*lock));
	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_ref *ref;

		if (!annotation->resolved)
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			unsigned long base;

			if (!matching_data_ref(ref, access, &base))
				continue;
			found = true;
			switch (annotation->kind) {
			case ANNOTATION_MUTEX_PROTECTS_DATA:
				policy->protection = LOCKLINT_PROTECTION_MUTEX;
				protector = annotation->lock;
				protector_base = base;
				protected_offset = ref->offset;
				break;
			case ANNOTATION_RWLOCK_PROTECTS_DATA:
				policy->protection = LOCKLINT_PROTECTION_RWLOCK;
				protector = annotation->lock;
				protector_base = base;
				protected_offset = ref->offset;
				break;
			case ANNOTATION_SCHEME_PROTECTS_DATA:
				policy->protection = LOCKLINT_PROTECTION_SCHEME;
				protector = NULL;
				break;
			case ANNOTATION_DATA_READABLE_WITHOUT_LOCK:
				policy->readable_without_lock = true;
				break;
			case ANNOTATION_READ_ONLY_DATA:
				policy->read_only = true;
				break;
			default:
				abort();
			}
		}
	}
	if (policy->protection == LOCKLINT_PROTECTION_MUTEX ||
	    policy->protection == LOCKLINT_PROTECTION_RWLOCK) {
		lock->root = protector->root != NULL ?
		    protector->root : access->root;
		lock->object = protector->root != NULL ?
		    protector->object : access->object;
		lock->type = protector->owner_type;
		lock->member = protector->member;
		lock->offset = (protector->root != NULL ? 0 :
		    protector_base) + protector->offset;
		lock->expr = NULL;
		lock->path = NULL;
		if (protector->root == NULL &&
		    access->address_base != NULL &&
		    protected_offset <= INT64_MAX &&
		    protector->offset <= INT64_MAX &&
		    access->address_offset >=
		    INT64_MIN + (int64_t)protected_offset &&
		    access->address_offset - (int64_t)protected_offset <=
		    INT64_MAX - (int64_t)protector->offset) {
			lock->address_base = access->address_base;
			lock->address_offset = access->address_offset -
			    (int64_t)protected_offset +
			    (int64_t)protector->offset;
		}
	}
	return (found);
}

/*
 * Present resolved adjacent LOCK_ORDER pairs without exposing parser-owned
 * annotation records.  Names are valid only for the duration of the callback;
 * resolved Sparse and identity pointers remain borrowed for the process.
 */
void
locklint_for_each_order_edge(locklint_order_edge_f callback, void *data)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_ref *left;

		if (!annotation->resolved ||
		    annotation->kind != ANNOTATION_LOCK_ORDER)
			continue;
		for (left = annotation->order; left != NULL &&
		    left->next != NULL; left = left->next) {
			struct annotation_ref *right = left->next;
			struct locklint_access left_access = { 0 };
			struct locklint_access right_access = { 0 };
			char *left_name;
			char *right_name;

			left_access.root = left->root;
			left_access.object = left->object;
			left_access.type = left->owner_type;
			left_access.member = left->member;
			left_access.offset = left->offset;
			right_access.root = right->root;
			right_access.object = right->object;
			right_access.type = right->owner_type;
			right_access.member = right->member;
			right_access.offset = right->offset;
			left_name = annotation_ref_name(left);
			right_name = annotation_ref_name(right);
			callback(&left_access, left_name, &right_access,
			    right_name, &annotation->pos, data);
			free(left_name);
			free(right_name);
		}
	}
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

static void
show_annotation_ref(FILE *stream, const struct annotation_ref *ref)
{
	if (ref->path == NULL) {
		(void) fputs(ref->base_name, stream);
		return;
	}
	(void) fprintf(stream, "%s%s%s", ref->base_name,
	    ref->scope == ANNOTATION_TYPE ? "::" : ".", ref->path);
}

static void
show_replacement(FILE *stream, const char *message,
    const struct annotation_ref *ref)
{
	(void) fprintf(stream, " [%s %s:%u:%u]", message,
	    stream_name(ref->annotation_pos.stream),
	    ref->annotation_pos.line, ref->annotation_pos.pos);
}

static const char *
annotation_kind_name(enum annotation_kind kind)
{
	switch (kind) {
	case ANNOTATION_MUTEX_PROTECTS_DATA:
		return ("MUTEX_PROTECTS_DATA");
	case ANNOTATION_RWLOCK_PROTECTS_DATA:
		return ("RWLOCK_PROTECTS_DATA");
	case ANNOTATION_SCHEME_PROTECTS_DATA:
		return ("SCHEME_PROTECTS_DATA");
	case ANNOTATION_DATA_READABLE_WITHOUT_LOCK:
		return ("DATA_READABLE_WITHOUT_LOCK");
	case ANNOTATION_READ_ONLY_DATA:
		return ("READ_ONLY_DATA");
	case ANNOTATION_LOCK_ORDER:
		return ("LOCK_ORDER");
	default:
		abort();
	}
}

void
locklint_show_annotations(FILE *stream)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_ref *ref;

		if (!annotation->resolved) {
			(void) fprintf(stream, "%s:%u:%u: ",
			    stream_name(annotation->pos.stream),
			    annotation->pos.line, annotation->pos.pos);
			show_raw_annotation(stream, annotation);
			(void) fputc('\n', stream);
			continue;
		}
		if (annotation->kind == ANNOTATION_LOCK_ORDER) {
			(void) fprintf(stream, "%s:%u:%u: %s ",
			    stream_name(annotation->pos.stream),
			    annotation->pos.line, annotation->pos.pos,
			    annotation_kind_name(annotation->kind));
			for (ref = annotation->order; ref != NULL;
			    ref = ref->next) {
				if (ref != annotation->order)
					(void) fputs(" -> ", stream);
				show_annotation_ref(stream, ref);
			}
			(void) fputc('\n', stream);
			continue;
		}
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			(void) fprintf(stream, "%s:%u:%u: %s ",
			    stream_name(annotation->pos.stream),
			    annotation->pos.line, annotation->pos.pos,
			    annotation_kind_name(annotation->kind));
			if (annotation->kind ==
			    ANNOTATION_MUTEX_PROTECTS_DATA ||
			    annotation->kind ==
			    ANNOTATION_RWLOCK_PROTECTS_DATA) {
				show_annotation_ref(stream, annotation->lock);
				(void) fputs(" -> ", stream);
			} else if (annotation->kind ==
			    ANNOTATION_SCHEME_PROTECTS_DATA) {
				(void) fprintf(stream, "%s -> ",
				    annotation->scheme);
			}
			show_annotation_ref(stream, ref);
			if (ref->replaced_by != NULL)
				show_replacement(stream, "replaced by",
				    ref->replaced_by);
			if (ref->replaces != NULL)
				show_replacement(stream, "replaces",
				    ref->replaces);
			(void) fputc('\n', stream);
		}
	}
}
