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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"
#include "lib.h"
#include "access.h"
#include "annotations.h"
#include "diagnostics.h"
#include "expression.h"
#include "identity.h"
#include "linearize.h"
#include "lock_identity.h"
#include "parse.h"
#include "scope.h"
#include "statistics.h"
#include "symbol.h"
#include "token.h"
#include "type.h"

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
	ANNOTATION_RWLOCK_COVERS_LOCKS,
	ANNOTATION_LOCK_ORDER
};

struct annotation_ref {
	struct position pos;
	enum annotation_scope scope;
	char *base_name;
	char *path;
	struct symbol *root;
	struct object_identity *object;
	const struct ll_type *owner_type;
	const struct type_member *member;
	unsigned long offset;
	struct annotation_ref *replaces;
	struct annotation_ref *replaced_by;
	struct position annotation_pos;
	struct annotation_ref *next;
};

struct annotation {
	struct position pos;
	struct translation_unit *tu;
	char *command_file;
	unsigned long command_line;
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

struct policy_ref_identity {
	const char *source;
	const char *scheme;
	struct symbol *lock_root;
	struct object_identity *lock_object;
	const struct ll_type *lock_owner;
	const struct type_member *lock_member;
	const struct ll_type *data_owner;
	const struct type_member *data_member;
	unsigned long line;
	unsigned long column;
	unsigned long lock_offset;
	unsigned long data_offset;
	enum annotation_kind kind;
	bool has_lock;
};

struct policy_ref_index_entry {
	struct policy_ref_identity identity;
	avl_node_t by_identity;
};

enum data_policy_key_kind {
	DATA_POLICY_KEY_TYPE,
	DATA_POLICY_KEY_OBJECT,
	DATA_POLICY_KEY_ROOT
};

/*
 * Index each retained data reference by the identity available on an access.
 * Sequence preserves annotation order when type and object candidate ranges
 * are merged.
 */
struct data_policy_index_entry {
	const void *identity;
	const struct annotation *annotation;
	const struct annotation_ref *ref;
	unsigned long sequence;
	enum data_policy_key_kind kind;
	avl_node_t by_identity;
};

static struct annotation *annotations;
static struct annotation **annotations_tail = &annotations;
static avl_tree_t policy_ref_index;
static avl_tree_t data_policy_index;
static unsigned long data_policy_sequence;

static enum locklint_execution_kind execution_kind(const struct token *);
static void expand_data_refs(struct annotation *);
static void index_data_policy_refs(const struct annotation *);

static int
policy_ref_identity_compare(const void *left_arg, const void *right_arg)
{
	const struct policy_ref_index_entry *left = left_arg;
	const struct policy_ref_index_entry *right = right_arg;
	const struct policy_ref_identity *a = &left->identity;
	const struct policy_ref_identity *b = &right->identity;
	int result;

	result = strcmp(a->source, b->source);
	if (result != 0)
		return (AVL_ISIGN(result));
	if (a->line != b->line)
		return (AVL_CMP(a->line, b->line));
	if (a->column != b->column)
		return (AVL_CMP(a->column, b->column));
	if (a->kind != b->kind)
		return (AVL_CMP(a->kind, b->kind));
	if (a->scheme == NULL || b->scheme == NULL) {
		if (a->scheme != b->scheme)
			return (AVL_PCMP(a->scheme, b->scheme));
	} else {
		result = strcmp(a->scheme, b->scheme);
		if (result != 0)
			return (AVL_ISIGN(result));
	}
	if (a->has_lock != b->has_lock)
		return (AVL_CMP(a->has_lock, b->has_lock));
	if (a->lock_root != b->lock_root)
		return (AVL_PCMP(a->lock_root, b->lock_root));
	if (a->lock_object != b->lock_object)
		return (AVL_PCMP(a->lock_object, b->lock_object));
	if (a->lock_owner != b->lock_owner)
		return (AVL_PCMP(a->lock_owner, b->lock_owner));
	if (a->lock_member != b->lock_member)
		return (AVL_PCMP(a->lock_member, b->lock_member));
	if (a->lock_offset != b->lock_offset)
		return (AVL_CMP(a->lock_offset, b->lock_offset));
	if (a->data_owner != b->data_owner)
		return (AVL_PCMP(a->data_owner, b->data_owner));
	if (a->data_member != b->data_member)
		return (AVL_PCMP(a->data_member, b->data_member));
	return (AVL_CMP(a->data_offset, b->data_offset));
}

static int
data_policy_index_compare(const void *left_arg, const void *right_arg)
{
	const struct data_policy_index_entry *left = left_arg;
	const struct data_policy_index_entry *right = right_arg;

	if (left->kind != right->kind)
		return (AVL_CMP(left->kind, right->kind));
	if (left->identity != right->identity)
		return (AVL_PCMP(left->identity, right->identity));
	return (AVL_CMP(left->sequence, right->sequence));
}

/*
 * Add resolved references after expansion and deduplication so lookup owns no
 * duplicate semantic records and can refer directly to process-lifetime
 * annotations.
 */
static void
index_data_policy_refs(const struct annotation *annotation)
{
	const struct annotation_ref *ref;

	if (annotation->kind == ANNOTATION_RWLOCK_COVERS_LOCKS ||
	    annotation->kind == ANNOTATION_LOCK_ORDER)
		return;
	for (ref = annotation->data; ref != NULL; ref = ref->next) {
		struct data_policy_index_entry *entry;
		avl_index_t where;

		entry = calloc(1, sizeof (*entry));
		if (entry == NULL)
			die("out of memory indexing data policy");
		entry->annotation = annotation;
		entry->ref = ref;
		entry->sequence = ++data_policy_sequence;
		if (ref->root == NULL) {
			entry->kind = DATA_POLICY_KEY_TYPE;
			entry->identity = ref->member;
		} else if (ref->object != NULL) {
			entry->kind = DATA_POLICY_KEY_OBJECT;
			entry->identity = ref->object;
		} else {
			entry->kind = DATA_POLICY_KEY_ROOT;
			entry->identity = ref->root;
		}
		if (avl_find(&data_policy_index, entry, &where) != NULL)
			abort();
		avl_insert(&data_policy_index, entry, where);
	}
}

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
	if (strcmp(text, "LOCK_UPGRADED_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_LOCK_UPGRADED_EFFECT);
	if (strcmp(text, "LOCK_DOWNGRADED_AS_SIDE_EFFECT") == 0)
		return (LOCKLINT_EXECUTION_LOCK_DOWNGRADED_EFFECT);
	if (strcmp(text, "NOT_REACHED") == 0)
		return (LOCKLINT_EXECUTION_NOT_REACHED);
	return (LOCKLINT_EXECUTION_NONE);
}

void
locklint_annotations_enable(void)
{
	avl_create(&policy_ref_index, policy_ref_identity_compare,
	    sizeof (struct policy_ref_index_entry),
	    offsetof(struct policy_ref_index_entry, by_identity));
	avl_create(&data_policy_index, data_policy_index_compare,
	    sizeof (struct data_policy_index_entry),
	    offsetof(struct data_policy_index_entry, by_identity));
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
	add_pre_buffer("#define LOCK_UPGRADED_AS_SIDE_EFFECT(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_LOCK_UPGRADED_EFFECT);
	add_pre_buffer("#define LOCK_DOWNGRADED_AS_SIDE_EFFECT(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_LOCK_DOWNGRADED_EFFECT);
	add_pre_buffer("#define NOW_INVISIBLE_TO_OTHER_THREADS(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_INVISIBLE);
	add_pre_buffer("#define NOW_VISIBLE_TO_OTHER_THREADS(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_VISIBLE);
	add_pre_buffer("#define ASSUMING_PROTECTED(...) "
	    "__context__((__VA_ARGS__), 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_ASSUME_PROTECTED);
	add_pre_buffer("#define NOT_REACHED "
	    "__context__(0, 0, %lu); __builtin_unreachable();\n",
	    (unsigned long)LOCKLINT_EXECUTION_NOT_REACHED);
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

static char *
join_name(const char *prefix, const char *suffix)
{
	char *name;
	size_t prefix_len = strlen(prefix);
	size_t suffix_len = strlen(suffix);

	name = malloc(prefix_len + suffix_len + 1);
	if (name == NULL)
		die("out of memory parsing locklint annotation name");
	(void) memcpy(name, prefix, prefix_len);
	(void) memcpy(name + prefix_len, suffix, suffix_len + 1);
	return (name);
}

static struct annotation_ref *
alloc_annotation_ref_name(struct annotation_token *base,
    enum annotation_scope scope, const char *base_name, const char *path)
{
	struct annotation_ref *ref;

	ref = calloc(1, sizeof (*ref));
	if (ref == NULL)
		die("out of memory parsing locklint annotation");
	ref->pos = base->pos;
	ref->scope = scope;
	ref->base_name = copy_string(base_name);
	if (path != NULL)
		ref->path = copy_string(path);
	return (ref);
}

static struct annotation_ref *
alloc_annotation_ref(struct annotation_token *base,
    enum annotation_scope scope, const char *path)
{
	return (alloc_annotation_ref_name(base, scope, base->text, path));
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

/*
 * Expand a suffix generator by concatenating each generated component with
 * the component immediately before the opening brace.
 */
static bool
parse_path_suffix_group(struct annotation *annotation,
    struct annotation_token **cursor, struct annotation_token *base,
    enum annotation_scope scope, const char *prefix,
    struct annotation_ref **head, struct annotation_ref ***tail)
{
	bool any = false;

	if (!token_is(*cursor, "{"))
		return (annotation_error(annotation, *cursor,
		    "expected '{' in annotation name suffix generator"));
	*cursor = (*cursor)->next;
	while (*cursor != NULL && !token_is(*cursor, "}")) {
		struct annotation_token *suffix = *cursor;
		char *path;

		if (token_is(suffix, ",")) {
			*cursor = suffix->next;
			continue;
		}
		if (suffix->type != TOKEN_IDENT)
			return (annotation_error(annotation, suffix,
			    "expected annotation name suffix"));
		path = join_name(prefix, suffix->text);
		*cursor = suffix->next;
		if (token_is(*cursor, ".")) {
			bool parsed;

			*cursor = (*cursor)->next;
			parsed = parse_path(annotation, cursor, base, scope,
			    path, head, tail);
			free(path);
			if (!parsed)
				return (false);
		} else if (token_is(*cursor, "{")) {
			bool parsed;

			parsed = parse_path_suffix_group(annotation, cursor,
			    base, scope, path, head, tail);
			free(path);
			if (!parsed)
				return (false);
		} else {
			add_annotation_ref(head, tail,
			    alloc_annotation_ref(base, scope, path));
			free(path);
		}
		any = true;
	}
	if (!token_is(*cursor, "}"))
		return (annotation_error(annotation, *cursor,
		    "expected '}' after annotation name suffix generator"));
	if (!any)
		return (annotation_error(annotation, *cursor,
		    "empty annotation name suffix generator"));
	*cursor = (*cursor)->next;
	return (true);
}

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
	if (token_is(*cursor, "{")) {
		bool parsed;

		parsed = parse_path_suffix_group(annotation, cursor, base, scope,
		    path, head, tail);
		free(path);
		return (parsed);
	}
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

/*
 * Expand a suffix generator applied to an unqualified object name.
 */
static bool
parse_name_suffix_group(struct annotation *annotation,
    struct annotation_token **cursor, struct annotation_token *base,
    struct annotation_ref **head, struct annotation_ref ***tail)
{
	bool any = false;

	if (!token_is(*cursor, "{"))
		return (annotation_error(annotation, *cursor,
		    "expected '{' in annotation name suffix generator"));
	*cursor = (*cursor)->next;
	while (*cursor != NULL && !token_is(*cursor, "}")) {
		struct annotation_token *suffix = *cursor;
		char *name;

		if (token_is(suffix, ",")) {
			*cursor = suffix->next;
			continue;
		}
		if (suffix->type != TOKEN_IDENT)
			return (annotation_error(annotation, suffix,
			    "expected annotation name suffix"));
		name = join_name(base->text, suffix->text);
		add_annotation_ref(head, tail,
		    alloc_annotation_ref_name(base, ANNOTATION_AUTO, name, NULL));
		free(name);
		*cursor = suffix->next;
		any = true;
	}
	if (!token_is(*cursor, "}"))
		return (annotation_error(annotation, *cursor,
		    "expected '}' after annotation name suffix generator"));
	if (!any)
		return (annotation_error(annotation, *cursor,
		    "empty annotation name suffix generator"));
	*cursor = (*cursor)->next;
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
	if (token_is(*cursor, "{")) {
		return (parse_name_suffix_group(annotation, cursor, base,
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
	} else if (token_is(cursor, "RWLOCK_COVERS_LOCKS")) {
		annotation->kind = ANNOTATION_RWLOCK_COVERS_LOCKS;
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
	case ANNOTATION_RWLOCK_COVERS_LOCKS:
		if (!parse_name(annotation, &cursor, &lock, &tail))
			return (false);
		if (lock->next != NULL) {
			const char *message;

			if (annotation->kind ==
			    ANNOTATION_MUTEX_PROTECTS_DATA) {
				message =
				    "MUTEX_PROTECTS_DATA requires one lock name";
			} else if (annotation->kind ==
			    ANNOTATION_RWLOCK_PROTECTS_DATA) {
				message =
				    "RWLOCK_PROTECTS_DATA requires one lock name";
			} else {
				message =
				    "RWLOCK_COVERS_LOCKS requires one cover lock";
			}
			return (annotation_error(annotation, cursor, message));
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

static bool
position_before(struct position left, struct position right)
{
	if (left.stream != right.stream)
		return (false);
	if (left.line != right.line)
		return (left.line < right.line);
	return (left.pos < right.pos);
}

static bool
position_in_range(struct position pos, struct position start,
    struct position end)
{
	return (pos.stream == start.stream && end.stream == start.stream &&
	    !position_before(pos, start) && !position_before(end, pos));
}

static void
find_local_in_statement(struct statement *stmt, struct ident *ident,
    struct position pos, struct symbol **result)
{
	struct statement *child;
	struct symbol *symbol;

	if (stmt == NULL)
		return;

	switch (stmt->type) {
	case STMT_COMPOUND:
		if (!position_in_range(pos, stmt->pos, stmt->endpos))
			return;
		FOR_EACH_PTR(stmt->stmts, child) {
			if (child->type == STMT_DECLARATION &&
			    !position_before(pos, child->pos)) {
				FOR_EACH_PTR(child->declaration, symbol) {
					if (symbol->namespace == NS_SYMBOL &&
					    symbol->ident == ident)
						*result = symbol;
				} END_FOR_EACH_PTR(symbol);
			}
			find_local_in_statement(child, ident, pos, result);
		} END_FOR_EACH_PTR(child);
		break;
	case STMT_IF:
		find_local_in_statement(stmt->if_true, ident, pos, result);
		find_local_in_statement(stmt->if_false, ident, pos, result);
		break;
	case STMT_ITERATOR:
		if (!position_in_range(pos, stmt->pos, stmt->endpos))
			return;
		FOR_EACH_PTR(stmt->iterator_syms, symbol) {
			if (symbol->namespace == NS_SYMBOL &&
			    symbol->ident == ident)
				*result = symbol;
		} END_FOR_EACH_PTR(symbol);
		find_local_in_statement(stmt->iterator_pre_statement, ident, pos,
		    result);
		find_local_in_statement(stmt->iterator_statement, ident, pos,
		    result);
		find_local_in_statement(stmt->iterator_post_statement, ident, pos,
		    result);
		break;
	case STMT_SWITCH:
		find_local_in_statement(stmt->switch_statement, ident, pos,
		    result);
		break;
	case STMT_CASE:
		find_local_in_statement(stmt->case_statement, ident, pos, result);
		break;
	case STMT_LABEL:
		find_local_in_statement(stmt->label_statement, ident, pos, result);
		break;
	default:
		break;
	}
}

/*
 * Recover the declaration visible at an annotation within a function body.
 * Sparse removes block declarations from identifier lookup after parsing,
 * but retains them in the function AST.
 */
static struct symbol *
resolve_local_in_functions(struct symbol_list *symbols, struct ident *ident,
    struct position pos)
{
	struct symbol *function;

	FOR_EACH_PTR(symbols, function) {
		struct symbol *argument;
		struct symbol *local = NULL;
		struct symbol *type = type_node_strip(function->ctype.base_type);
		struct statement *body;

		if (type == NULL || type->type != SYM_FN)
			continue;
		body = type->stmt != NULL ? type->stmt : type->inline_stmt;
		if (body == NULL ||
		    !position_in_range(pos, body->pos, body->endpos))
			continue;
		FOR_EACH_PTR(type->arguments, argument) {
			if (argument->namespace == NS_SYMBOL &&
			    argument->ident == ident)
				local = argument;
		} END_FOR_EACH_PTR(argument);
		find_local_in_statement(body, ident, pos, &local);
		if (local != NULL)
			return (local);
	} END_FOR_EACH_PTR(function);
	return (NULL);
}

static struct symbol *
resolve_local(struct symbol_list *symbols, struct ident *ident,
    struct position pos)
{
	struct symbol *local;

	local = resolve_local_in_functions(symbols, ident, pos);
	if (local == NULL)
		local = resolve_local_in_functions(file_scope->symbols, ident, pos);
	if (local == NULL)
		local = resolve_local_in_functions(global_scope->symbols, ident,
		    pos);
	return (local);
}

static bool
valid_identifier(const char *start, size_t length)
{
	size_t i;

	if (length == 0 ||
	    !((*start >= 'a' && *start <= 'z') ||
	    (*start >= 'A' && *start <= 'Z') || *start == '_'))
		return (false);
	for (i = 1; i < length; i++) {
		char c = start[i];

		if (!((c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_'))
			return (false);
	}
	return (true);
}

static bool
valid_member_path(const char *path)
{
	const char *component = path;
	const char *end;

	do {
		end = strchr(component, '.');
		if (!valid_identifier(component, end != NULL ?
		    (size_t)(end - component) : strlen(component)))
			return (false);
		component = end != NULL ? end + 1 : NULL;
	} while (component != NULL);
	return (true);
}

static struct annotation_ref *
new_command_ref(const char *base, size_t base_length, const char *path,
    enum annotation_scope scope)
{
	struct annotation_ref *ref;

	ref = calloc(1, sizeof (*ref));
	if (ref == NULL)
		die("out of memory recording command-file declaration");
	ref->base_name = malloc(base_length + 1);
	if (ref->base_name == NULL)
		die("out of memory recording command-file declaration");
	(void) memcpy(ref->base_name, base, base_length);
	ref->base_name[base_length] = '\0';
	if (path != NULL)
		ref->path = copy_string(path);
	ref->scope = scope;
	return (ref);
}

static bool
resolve_command_path(struct annotation_ref *ref, struct symbol *type)
{
	const char *component = ref->path;
	const char *end;

	while (component != NULL && *component != '\0') {
		struct ident *ident;
		struct symbol *member;
		char *name;
		size_t length;
		int offset = 0;

		type = type_compound_resolve(type);
		if (type == NULL)
			return (false);
		end = strchr(component, '.');
		length = end != NULL ? (size_t)(end - component) :
		    strlen(component);
		name = malloc(length + 1);
		if (name == NULL)
			die("out of memory resolving command-file declaration");
		(void) memcpy(name, component, length);
		name[length] = '\0';
		ident = built_in_ident(name);
		free(name);
		member = find_identifier(ident, type->symbol_list, &offset);
		if (member == NULL)
			return (false);
		ref->member = type_member_lookup_exact(member);
		if (ref->member == NULL)
			die("missing canonical command-file member");
		ref->offset += offset;
		type = member->ctype.base_type;
		component = end != NULL ? end + 1 : NULL;
	}
	return (true);
}

struct annotation_type_resolution {
	const char *name;
	const char *path;
	struct annotation_ref **tail;
	const struct ll_type *first_type;
	size_t base_length;
	enum locklint_command_result result;
};

static bool
annotations_type_check(const struct ll_type *type, void *data_arg)
{
	struct annotation_type_resolution *data = data_arg;

	if (data->first_type == NULL) {
		data->first_type = type;
		return (true);
	}
	if (type_layout_equal(data->first_type, type))
		return (true);
	data->result = LOCKLINT_COMMAND_INCONSISTENT_TYPE;
	return (false);
}

static bool
annotations_type_resolve(const struct ll_type *type, void *data_arg)
{
	struct annotation_type_resolution *data = data_arg;
	struct annotation_ref *ref;
	struct symbol *exact = type_representative(type);

	ref = new_command_ref(data->name, data->base_length, data->path,
	    ANNOTATION_TYPE);
	ref->owner_type = type;
	if (!resolve_command_path(ref, exact)) {
		data->result = LOCKLINT_COMMAND_UNRESOLVED_NAME;
		return (false);
	}
	*data->tail = ref;
	data->tail = &ref->next;
	data->result = LOCKLINT_COMMAND_OK;
	return (true);
}

static enum locklint_command_result
resolve_command_type(struct annotation *annotation, const char *name,
    const char *separator)
{
	struct annotation_type_resolution data = {
		.name = name,
		.path = separator + 2,
		.tail = &annotation->data,
		.base_length = (size_t)(separator - name),
		.result = LOCKLINT_COMMAND_UNRESOLVED_NAME
	};
	struct ident *ident;

	if (!valid_identifier(name, data.base_length) ||
	    !valid_member_path(separator + 2))
		return (LOCKLINT_COMMAND_INVALID_NAME);
	{
		char *base = malloc(data.base_length + 1);

		if (base == NULL)
			die("out of memory resolving command-file declaration");
		(void) memcpy(base, name, data.base_length);
		base[data.base_length] = '\0';
		ident = built_in_ident(base);
		free(base);
	}
	type_name_visit_types(ident, annotations_type_check, &data);
	if (data.result == LOCKLINT_COMMAND_INCONSISTENT_TYPE)
		return (data.result);
	if (data.first_type == NULL)
		return (LOCKLINT_COMMAND_UNRESOLVED_NAME);
	type_name_visit_types(ident, annotations_type_resolve, &data);
	return (data.result);
}

static enum locklint_command_result
resolve_command_object(struct annotation *annotation, const char *name)
{
	struct object_identity *object;
	struct annotation_ref *ref;
	struct symbol *root;
	struct symbol *type;
	const char *dot = strchr(name, '.');
	size_t base_length = dot != NULL ? (size_t)(dot - name) : strlen(name);

	if (!valid_identifier(name, base_length) ||
	    (dot != NULL && !valid_member_path(dot + 1)))
		return (LOCKLINT_COMMAND_INVALID_NAME);
	{
		char *base = malloc(base_length + 1);

		if (base == NULL)
			die("out of memory resolving command-file declaration");
		(void) memcpy(base, name, base_length);
		base[base_length] = '\0';
		object = locklint_external_object(base, &root);
		free(base);
	}
	if (object == NULL)
		return (LOCKLINT_COMMAND_UNRESOLVED_NAME);
	ref = new_command_ref(name, base_length, dot != NULL ? dot + 1 : NULL,
	    ANNOTATION_OBJECT);
	ref->root = root;
	ref->object = object;
	type = root->ctype.base_type;
	ref->owner_type = type_lookup_exact(type_compound_resolve(type));
	if (ref->path != NULL && !resolve_command_path(ref, type))
		return (LOCKLINT_COMMAND_UNRESOLVED_NAME);
	annotation->data = ref;
	return (LOCKLINT_COMMAND_OK);
}

/*
 * Add command-file readable policy to the same annotation list queried for
 * source DATA_READABLE_WITHOUT_LOCK declarations.
 */
enum locklint_command_result
locklint_declare_readable(const char *name, const char *file,
    unsigned long line)
{
	struct annotation *annotation;
	const char *dot;
	const char *separator;
	enum locklint_command_result result;

	annotation = calloc(1, sizeof (*annotation));
	if (annotation == NULL)
		die("out of memory recording command-file declaration");
	annotation->kind = ANNOTATION_DATA_READABLE_WITHOUT_LOCK;
	annotation->command_file = copy_string(file);
	annotation->command_line = line;

	separator = strstr(name, "::");
	dot = strchr(name, '.');
	if (separator != NULL &&
	    (strstr(separator + 2, "::") != NULL ||
	    (dot != NULL && dot < separator))) {
		result = LOCKLINT_COMMAND_INVALID_NAME;
	} else if (separator != NULL) {
		result = resolve_command_type(annotation, name, separator);
	} else {
		result = resolve_command_object(annotation, name);
	}
	if (result != LOCKLINT_COMMAND_OK)
		return (result);
	expand_data_refs(annotation);
	annotation->parsed = true;
	annotation->processed = true;
	annotation->resolved = true;
	*annotations_tail = annotation;
	annotations_tail = &annotation->next;
	index_data_policy_refs(annotation);
	return (LOCKLINT_COMMAND_OK);
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
resolve_path(struct annotation_ref *ref, struct symbol *type,
    struct symbol **resolved_type)
{
	const char *component = ref->path;
	const char *end;

	while (component != NULL && *component != '\0') {
		struct ident *ident;
		struct symbol *member;
		char *name;
		size_t length;
		int offset = 0;

		type = type_compound_resolve(type);
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
		ref->member = type_member_lookup_exact(member);
		if (ref->member == NULL) {
			if (!type_registry_consistent())
				return (false);
			die("missing canonical annotation member '%s' at %s:%u:%u",
			    show_ident(member->ident),
			    stream_name(member->pos.stream), member->pos.line,
			    member->pos.pos);
		}
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
	*resolved_type = type;
	return (true);
}

/*
 * Resolve automatic names in the current translation unit, preferring an
 * object over a type, and retain canonical identity only for object scope.
 */
static bool
resolve_annotation_ref(struct annotation_ref *ref, bool lock,
    struct translation_unit *tu, struct symbol_list *symbols)
{
	struct ident *ident = built_in_ident(ref->base_name);
	struct symbol *base;
	struct symbol *type;

	if (ref->scope == ANNOTATION_TYPE) {
		base = resolve_type(ref->base_name);
	} else {
		base = resolve_local(symbols, ident, ref->pos);
		if (base == NULL)
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
		type = base->ctype.base_type;
	} else {
		type = base;
	}
	ref->owner_type = type_lookup_exact(type_compound_resolve(type));
	if (ref->path != NULL && !resolve_path(ref, type, &type))
		return (false);
	if (lock && ref->scope == ANNOTATION_TYPE && ref->member == NULL &&
	    type_compound_resolve(type) != NULL) {
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
	ref->member = type_member_lookup_exact(member);
	if (ref->member == NULL && type_registry_consistent())
		die("missing canonical expanded member");
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

	type = type_compound_resolve(type);
	if (type == NULL) {
		add_annotation_ref(head, tail, source);
		return;
	}

	FOR_EACH_PTR(type->symbol_list, member) {
		struct annotation_ref *expanded;
		struct symbol *member_type;
		char *path;

		if (member->ident == NULL) {
			member_type = type_compound_resolve(
			    member->ctype.base_type);
			if (member_type != NULL) {
				expanded = clone_expanded_ref(source, member,
				    source->path);
				expand_compound_ref(expanded, member_type, lock,
				    head, tail);
			}
			continue;
		}
		if (lock != NULL &&
		    type_member_lookup_exact(member) == lock->member) {
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
		member_type = type_compound_resolve(member->ctype.base_type);
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
		struct symbol *type;

		if (ref->member != NULL) {
			type = ref->member->representative->ctype.base_type;
		} else if (ref->owner_type != NULL) {
			type = type_representative(ref->owner_type);
		} else {
			type = ref->root != NULL ?
			    ref->root->ctype.base_type : NULL;
		}
		type = type_compound_resolve(type);

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

/*
 * Retain one expanded type-scoped reference for each source declaration and
 * canonical policy identity.  Repeated header instances have the same source
 * position and canonical references; separately written declarations do not.
 */
static void
deduplicate_type_policy_refs(struct annotation *annotation)
{
	struct annotation_ref **link = &annotation->data;

	if (annotation->command_file != NULL)
		return;
	while (*link != NULL) {
		struct annotation_ref *ref = *link;
		struct policy_ref_index_entry key = {
			.identity = {
				.source = stream_name(annotation->pos.stream),
				.scheme = annotation->scheme,
				.lock_root = annotation->lock != NULL &&
				    annotation->lock->object == NULL ?
				    annotation->lock->root : NULL,
				.lock_object = annotation->lock != NULL ?
				    annotation->lock->object : NULL,
				.lock_owner = annotation->lock != NULL ?
				    annotation->lock->owner_type : NULL,
				.lock_member = annotation->lock != NULL ?
				    annotation->lock->member : NULL,
				.data_owner = ref->owner_type,
				.data_member = ref->member,
				.line = annotation->pos.line,
				.column = annotation->pos.pos,
				.lock_offset = annotation->lock != NULL ?
				    annotation->lock->offset : 0,
				.data_offset = ref->offset,
				.kind = annotation->kind,
				.has_lock = annotation->lock != NULL
			}
		};
		struct policy_ref_index_entry *entry;
		avl_index_t where;

		if (ref->scope != ANNOTATION_TYPE) {
			link = &ref->next;
			continue;
		}
		statistics.source_type_policy_refs_resolved++;
		entry = avl_find(&policy_ref_index, &key, &where);
		if (entry == NULL) {
			entry = malloc(sizeof (*entry));
			if (entry == NULL)
				die("out of memory indexing canonical policy");
			*entry = key;
			avl_insert(&policy_ref_index, entry, where);
			statistics.source_type_policy_refs_retained++;
			link = &ref->next;
			continue;
		}
		statistics.source_type_policy_refs_deduplicated++;
		*link = ref->next;
		free(ref->base_name);
		free(ref->path);
		free(ref);
	}
}

static bool
same_data_ref(const struct annotation_ref *left,
    const struct annotation_ref *right)
{
	/*
	 * Object annotations compare canonical roots across translation units.
	 * Type annotations already contain canonical owner and member identities.
	 */
	if (left->root != NULL || right->root != NULL) {
		struct locklint_access left_access = { 0 };
		struct locklint_access right_access = { 0 };

		if (left->root == NULL || right->root == NULL)
			return (false);
		left_access.root = left->root;
		left_access.object = left->object;
		left_access.member = left->member != NULL ?
		    left->member->representative : NULL;
		left_access.offset = left->offset;
		right_access.root = right->root;
		right_access.object = right->object;
		right_access.member = right->member != NULL ?
		    right->member->representative : NULL;
		right_access.offset = right->offset;
		return (locklint_same_access(&left_access, &right_access));
	}
	return (left->owner_type == right->owner_type &&
	    left->member == right->member &&
	    left->offset == right->offset);
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
locklint_resolve_annotations(struct symbol_list *symbols)
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
				    annotation->tu, symbols))
					break;
			}
			if (ref == NULL)
				annotation->resolved = true;
			continue;
		}
		if (annotation->lock != NULL &&
		    !resolve_annotation_ref(annotation->lock, true,
		    annotation->tu, symbols))
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			if (!resolve_annotation_ref(ref,
			    annotation->kind == ANNOTATION_RWLOCK_COVERS_LOCKS,
			    annotation->tu, symbols))
				break;
		}
		if (ref == NULL) {
			if (annotation->kind !=
			    ANNOTATION_RWLOCK_COVERS_LOCKS)
				expand_data_refs(annotation);
			deduplicate_type_policy_refs(annotation);
			if (annotation->kind ==
			    ANNOTATION_MUTEX_PROTECTS_DATA ||
			    annotation->kind ==
			    ANNOTATION_RWLOCK_PROTECTS_DATA ||
			    annotation->kind ==
			    ANNOTATION_SCHEME_PROTECTS_DATA)
				record_replacements(annotation);
			annotation->resolved = true;
			index_data_policy_refs(annotation);
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
	case LOCKLINT_EXECUTION_LOCK_UPGRADED_EFFECT:
	case LOCKLINT_EXECUTION_LOCK_DOWNGRADED_EFFECT:
	case LOCKLINT_EXECUTION_ASSERT_NO_COMPETITION:
	case LOCKLINT_EXECUTION_NOT_REACHED:
		return ((enum locklint_execution_kind)insn->context_tag);
	default:
		return (LOCKLINT_EXECUTION_NONE);
	}
}

/*
 * Visit the source operands of one visibility marker in left-to-right order.
 * Sparse represents a variadic annotation argument list as a comma-expression
 * tree, so flattening that tree avoids both silently dropping later targets
 * and allocating a normally tiny temporary vector.
 */
static void
for_each_visibility_operand(struct translation_unit *tu,
    struct expression *expr, locklint_visibility_target_f callback, void *data)
{
	struct locklint_access access;
	struct lock_identity_key key;
	enum lock_analysis_object_type object_type;
	uint64_t length;

	if (expr != NULL && expr->type == EXPR_COMMA) {
		for_each_visibility_operand(tu, expr->left, callback, data);
		for_each_visibility_operand(tu, expr->right, callback, data);
		return;
	}
	if (expr != NULL && locklint_get_access(tu, expr, &access) &&
	    lock_identity_key_from_access(&access, &key, &object_type) == 0 &&
	    locklint_access_size(&access, &length))
		callback(&access, expr, data);
	else
		callback(NULL, expr, data);
}

bool
locklint_for_each_visibility_target(struct translation_unit *tu,
    const struct instruction *insn, locklint_visibility_target_f callback,
    void *data)
{
	enum locklint_execution_kind kind =
	    locklint_get_execution_annotation(insn);

	if (kind != LOCKLINT_EXECUTION_INVISIBLE &&
	    kind != LOCKLINT_EXECUTION_VISIBLE)
		return (false);
	for_each_visibility_operand(tu, insn->context_expr, callback, data);
	return (true);
}

bool
locklint_for_each_assumed_target(struct translation_unit *tu,
    const struct instruction *insn, locklint_visibility_target_f callback,
    void *data)
{
	if (locklint_get_execution_annotation(insn) !=
	    LOCKLINT_EXECUTION_ASSUME_PROTECTED)
		return (false);
	for_each_visibility_operand(tu, insn->context_expr, callback, data);
	return (true);
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
	case LOCKLINT_DECLARED_LOCK_UPGRADED:
		return ("LOCK_UPGRADED_AS_SIDE_EFFECT");
	case LOCKLINT_DECLARED_LOCK_DOWNGRADED:
		return ("LOCK_DOWNGRADED_AS_SIDE_EFFECT");
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
	case LOCKLINT_EXECUTION_LOCK_UPGRADED_EFFECT:
		*effect = LOCKLINT_DECLARED_LOCK_UPGRADED;
		break;
	case LOCKLINT_EXECUTION_LOCK_DOWNGRADED_EFFECT:
		*effect = LOCKLINT_DECLARED_LOCK_DOWNGRADED;
		break;
	default:
		return (false);
	}

	return (insn->context_expr != NULL &&
	    locklint_get_access(tu, insn->context_expr, lock));
}

static void
diagnose_visibility_target(const struct locklint_access *access,
    const struct expression *expr, void *data)
{
	const struct instruction *insn = data;

	(void) expr;
	if (access != NULL)
		return;
	locklint_warning(LOCKLINT_DIAG_VISIBILITY_NO_OBJECT,
	    insn->pos, "visibility annotation has no object");
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
			if (locklint_for_each_visibility_target(tu, insn,
			    diagnose_visibility_target, insn))
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
matching_data_ref_canonical(const struct annotation_ref *ref,
    const struct locklint_access *access,
    const struct type_member *access_member, unsigned long *base)
{
	if (ref->root != NULL) {
		struct locklint_access target = { 0 };

		target.root = ref->root;
		target.object = ref->object;
		target.member = ref->member != NULL ?
		    ref->member->representative : NULL;
		target.offset = ref->offset;
		if (!locklint_same_access(&target, access))
			return (false);
		*base = 0;
		return (true);
	}
	if (ref->member != access_member ||
	    !locklint_access_base_canonical(access,
	    ref->owner_type, ref->offset, base))
		return (false);
	return (true);
}

static bool
matching_data_ref(const struct annotation_ref *ref,
    const struct locklint_access *access, unsigned long *base)
{
	return (matching_data_ref_canonical(ref, access,
	    type_member_lookup_exact(access->member), base));
}

struct data_policy_cursor {
	struct data_policy_index_entry *entry;
	enum data_policy_key_kind kind;
	const void *identity;
};

/*
 * Position a cursor at the first entry for one lookup identity.  Sequence
 * zero sorts before every indexed reference.
 */
static void
data_policy_cursor_init(struct data_policy_cursor *cursor,
    enum data_policy_key_kind kind, const void *identity)
{
	struct data_policy_index_entry key = {
		.identity = identity,
		.kind = kind
	};
	avl_index_t where;

	cursor->kind = kind;
	cursor->identity = identity;
	cursor->entry = avl_find(&data_policy_index, &key, &where);
	if (cursor->entry == NULL)
		cursor->entry = avl_nearest(&data_policy_index, where, AVL_AFTER);
	if (cursor->entry != NULL &&
	    (cursor->entry->kind != kind ||
	    cursor->entry->identity != identity))
		cursor->entry = NULL;
}

static struct data_policy_index_entry *
data_policy_cursor_take(struct data_policy_cursor *cursor)
{
	struct data_policy_index_entry *entry = cursor->entry;

	if (entry == NULL)
		return (NULL);
	cursor->entry = AVL_NEXT(&data_policy_index, entry);
	if (cursor->entry != NULL &&
	    (cursor->entry->kind != cursor->kind ||
	    cursor->entry->identity != cursor->identity))
		cursor->entry = NULL;
	return (entry);
}

static void
annotation_ref_access(const struct annotation_ref *ref,
    struct locklint_access *access)
{
	*access = (struct locklint_access){ 0 };
	access->root = ref->root;
	access->object = ref->object;
	access->type = type_representative(ref->owner_type);
	access->member = ref->member != NULL ?
	    ref->member->representative : NULL;
	access->offset = ref->offset;
}

bool
locklint_get_covering_lock(const struct locklint_access *covered,
    struct locklint_access *cover)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_ref *ref;

		if (!annotation->resolved ||
		    annotation->kind != ANNOTATION_RWLOCK_COVERS_LOCKS)
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			unsigned long base;

			if (!matching_data_ref(ref, covered, &base))
				continue;
			annotation_ref_access(annotation->lock, cover);
			return (true);
		}
	}
	return (false);
}

bool
locklint_lock_covers(const struct locklint_access *cover,
    const struct locklint_access *covered)
{
	struct annotation *annotation;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_ref *ref;
		unsigned long base;

		if (!annotation->resolved ||
		    annotation->kind != ANNOTATION_RWLOCK_COVERS_LOCKS ||
		    !matching_data_ref(annotation->lock, cover, &base))
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			if (matching_data_ref(ref, covered, &base))
				return (true);
		}
	}
	return (false);
}

/*
 * Merge canonical type and object candidate ranges in original annotation
 * order.  The last mechanical or scheme declaration wins; unlocked-read and
 * read-only properties are additive.
 */
bool
locklint_data_policy(const struct locklint_access *access,
    struct locklint_data_policy *policy, struct locklint_access *lock)
{
	struct data_policy_cursor type_cursor = { 0 };
	struct data_policy_cursor object_cursor = { 0 };
	struct data_policy_index_entry *type_entry = NULL;
	struct data_policy_index_entry *object_entry = NULL;
	const struct annotation_ref *protector = NULL;
	const struct type_member *access_member;
	unsigned long protector_base = 0;
	const struct ll_type *protected_owner = NULL;
	bool found = false;

	(void) memset(policy, 0, sizeof (*policy));
	(void) memset(lock, 0, sizeof (*lock));
	statistics.data_policy_queries++;
	access_member = type_member_lookup_exact(access->member);
	if (access_member != NULL) {
		data_policy_cursor_init(&type_cursor, DATA_POLICY_KEY_TYPE,
		    access_member);
		type_entry = data_policy_cursor_take(&type_cursor);
	}
	if (access->object != NULL) {
		data_policy_cursor_init(&object_cursor, DATA_POLICY_KEY_OBJECT,
		    access->object);
		object_entry = data_policy_cursor_take(&object_cursor);
	} else if (access->root != NULL) {
		data_policy_cursor_init(&object_cursor, DATA_POLICY_KEY_ROOT,
		    access->root);
		object_entry = data_policy_cursor_take(&object_cursor);
	}
	while (type_entry != NULL || object_entry != NULL) {
		struct data_policy_index_entry *entry;
		const struct annotation *annotation;
		const struct annotation_ref *ref;
		unsigned long base;

		if (object_entry == NULL ||
		    (type_entry != NULL &&
		    type_entry->sequence < object_entry->sequence)) {
			entry = type_entry;
			type_entry = data_policy_cursor_take(&type_cursor);
		} else {
			entry = object_entry;
			object_entry = data_policy_cursor_take(&object_cursor);
		}
		statistics.data_policy_candidates++;
		annotation = entry->annotation;
		ref = entry->ref;
		if (!matching_data_ref_canonical(ref, access, access_member,
		    &base))
			continue;
		found = true;
		switch (annotation->kind) {
		case ANNOTATION_MUTEX_PROTECTS_DATA:
			policy->protection = LOCKLINT_PROTECTION_MUTEX;
			protector = annotation->lock;
			protector_base = base;
			protected_owner = ref->owner_type;
			break;
		case ANNOTATION_RWLOCK_PROTECTS_DATA:
			policy->protection = LOCKLINT_PROTECTION_RWLOCK;
			protector = annotation->lock;
			protector_base = base;
			protected_owner = ref->owner_type;
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
	if (policy->protection == LOCKLINT_PROTECTION_MUTEX ||
	    policy->protection == LOCKLINT_PROTECTION_RWLOCK) {
		unsigned long lock_base = protector_base;
		unsigned long access_from_lock;
		bool have_lock_base = true;

		if (protector->root == NULL &&
		    protector->owner_type != protected_owner) {
			have_lock_base =
			    locklint_access_containing_base_canonical(access,
			    protector->owner_type, protector_base, &lock_base);
		}
		lock->root = protector->root != NULL ?
		    protector->root : access->root;
		lock->object = protector->root != NULL ?
		    protector->object : access->object;
		lock->type = type_representative(protector->owner_type);
		lock->member = protector->member != NULL ?
		    protector->member->representative : NULL;
		lock->offset = (protector->root != NULL ? 0 :
		    (have_lock_base ? lock_base : protector_base)) +
		    protector->offset;
		lock->expr = NULL;
		lock->path = NULL;
		if (protector->root == NULL &&
		    have_lock_base &&
		    access->address_base != NULL &&
		    access->offset >= lock_base &&
		    (access_from_lock = access->offset - lock_base) <=
		    INT64_MAX &&
		    protector->offset <= INT64_MAX &&
		    access->address_offset >=
		    INT64_MIN + (int64_t)access_from_lock &&
		    access->address_offset - (int64_t)access_from_lock <=
		    INT64_MAX - (int64_t)protector->offset) {
			lock->address_base = access->address_base;
			lock->address_offset = access->address_offset -
			    (int64_t)access_from_lock +
			    (int64_t)protector->offset;
			lock->address_base_is_symbol =
			    access->address_base_is_symbol;
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
			left_access.type = type_representative(left->owner_type);
			left_access.member = left->member != NULL ?
			    left->member->representative : NULL;
			left_access.offset = left->offset;
			right_access.root = right->root;
			right_access.object = right->object;
			right_access.type =
			    type_representative(right->owner_type);
			right_access.member = right->member != NULL ?
			    right->member->representative : NULL;
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
show_annotation_location(FILE *stream, const struct annotation *annotation)
{
	if (annotation->command_file != NULL) {
		(void) fprintf(stream, "%s:%lu: ", annotation->command_file,
		    annotation->command_line);
	} else {
		(void) fprintf(stream, "%s:%u:%u: ",
		    stream_name(annotation->pos.stream),
		    annotation->pos.line, annotation->pos.pos);
	}
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
	case ANNOTATION_RWLOCK_COVERS_LOCKS:
		return ("RWLOCK_COVERS_LOCKS");
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
			show_annotation_location(stream, annotation);
			show_raw_annotation(stream, annotation);
			(void) fputc('\n', stream);
			continue;
		}
		if (annotation->kind == ANNOTATION_LOCK_ORDER) {
			show_annotation_location(stream, annotation);
			(void) fprintf(stream, "%s ",
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
			show_annotation_location(stream, annotation);
			(void) fprintf(stream, "%s ",
			    annotation_kind_name(annotation->kind));
			if (annotation->kind ==
			    ANNOTATION_MUTEX_PROTECTS_DATA ||
			    annotation->kind ==
			    ANNOTATION_RWLOCK_PROTECTS_DATA ||
			    annotation->kind ==
			    ANNOTATION_RWLOCK_COVERS_LOCKS) {
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
