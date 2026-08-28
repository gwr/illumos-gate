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

static int
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
parse_annotation(struct annotation *annotation)
{
	struct annotation_token *cursor = annotation->tokens;
	struct annotation_ref *lock = NULL;
	struct annotation_ref **tail = NULL;

	if (!token_is(cursor, "MUTEX_PROTECTS_DATA"))
		return (false);
	cursor = cursor->next;
	if (!token_is(cursor, "("))
		return (annotation_error(annotation, cursor,
		    "expected '(' after MUTEX_PROTECTS_DATA"));
	cursor = cursor->next;
	if (!parse_name(annotation, &cursor, &lock, &tail))
		return (false);
	if (lock->next != NULL)
		return (annotation_error(annotation, cursor,
		    "MUTEX_PROTECTS_DATA requires one lock name"));
	if (!token_is(cursor, ","))
		return (annotation_error(annotation, cursor,
		    "expected ',' after protected-data lock"));
	cursor = cursor->next;
	if (!parse_name_list(annotation, &cursor, &annotation->data, ")"))
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
	if (lock && ref->member == NULL &&
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
		if (member == lock->member) {
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

			if (!earlier->resolved)
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
		if (!resolve_annotation_ref(annotation->lock, true,
		    annotation->tu))
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			if (!resolve_annotation_ref(ref, false, annotation->tu))
				break;
		}
		if (ref == NULL) {
			expand_data_refs(annotation);
			record_replacements(annotation);
			annotation->resolved = true;
		}
	}
}

bool
locklint_protecting_access(const struct locklint_access *access,
    struct locklint_access *lock)
{
	struct annotation *annotation;
	const struct annotation_ref *protector = NULL;
	unsigned long protector_base = 0;

	for (annotation = annotations; annotation != NULL;
	    annotation = annotation->next) {
		struct annotation_ref *ref;

		if (!annotation->resolved)
			continue;
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			unsigned long base;

			if (ref->root != NULL) {
				struct locklint_access target = { 0 };

				target.root = ref->root;
				target.object = ref->object;
				target.member = ref->member;
				target.offset = ref->offset;
				if (!locklint_same_access(&target, access))
					continue;
				base = 0;
			} else {
				if (ref->member != access->member ||
				    !locklint_access_base(access,
				    ref->owner_type, ref->offset, &base))
					continue;
			}
			protector = annotation->lock;
			protector_base = base;
		}
	}
	if (protector == NULL)
		return (false);
	lock->root = protector->root != NULL ?
	    protector->root : access->root;
	lock->object = protector->root != NULL ?
	    protector->object : access->object;
	lock->type = protector->owner_type;
	lock->member = protector->member;
	lock->offset = (protector->root != NULL ? 0 : protector_base) +
	    protector->offset;
	lock->expr = NULL;
	return (true);
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
		for (ref = annotation->data; ref != NULL; ref = ref->next) {
			(void) fprintf(stream,
			    "%s:%u:%u: MUTEX_PROTECTS_DATA ",
			    stream_name(annotation->pos.stream),
			    annotation->pos.line, annotation->pos.pos);
			show_annotation_ref(stream, annotation->lock);
			(void) fputs(" -> ", stream);
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
