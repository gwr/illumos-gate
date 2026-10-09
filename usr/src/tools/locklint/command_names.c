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
 * Expand grouped command data names into the ordinary object and type-member
 * names consumed by semantic resolution.  This mirrors the established
 * source-annotation name grammar without retaining parser state after one
 * command has been handled.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "command_names.h"

enum command_name_token_kind {
	COMMAND_NAME_END,
	COMMAND_NAME_IDENT,
	COMMAND_NAME_DOT,
	COMMAND_NAME_TYPE_SEPARATOR,
	COMMAND_NAME_LEFT_BRACE,
	COMMAND_NAME_RIGHT_BRACE,
	COMMAND_NAME_COMMA,
	COMMAND_NAME_INVALID
};

struct command_name_token {
	enum command_name_token_kind kind;
	const char *text;
	size_t length;
};

struct command_name_parser {
	struct command_name_list *list;
	struct command_name_token token;
	const char *cursor;
	size_t capacity;
};

static bool parse_path(struct command_name_parser *, const char *);

static bool
name_character(char c, bool first)
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
		return (true);
	return (!first && c >= '0' && c <= '9');
}

static char *
copy_text(const char *text, size_t length)
{
	char *copy = malloc(length + 1);

	if (copy == NULL)
		return (NULL);
	(void) memcpy(copy, text, length);
	copy[length] = '\0';
	return (copy);
}

static bool
set_error(struct command_name_parser *parser, const char *message)
{
	if (parser->list->error == NULL)
		parser->list->error = message;
	return (false);
}

static void
next_token(struct command_name_parser *parser)
{
	const char *start;

	while (*parser->cursor == ' ' || *parser->cursor == '\t' ||
	    *parser->cursor == '\r' || *parser->cursor == '\n')
		parser->cursor++;
	start = parser->cursor;
	parser->token.text = start;
	parser->token.length = 1;

	switch (*start) {
	case '\0':
		parser->token.kind = COMMAND_NAME_END;
		parser->token.length = 0;
		return;
	case '.':
		parser->token.kind = COMMAND_NAME_DOT;
		parser->cursor++;
		return;
	case ':':
		if (start[1] == ':') {
			parser->token.kind = COMMAND_NAME_TYPE_SEPARATOR;
			parser->token.length = 2;
			parser->cursor += 2;
			return;
		}
		parser->token.kind = COMMAND_NAME_INVALID;
		parser->cursor++;
		return;
	case '{':
		parser->token.kind = COMMAND_NAME_LEFT_BRACE;
		parser->cursor++;
		return;
	case '}':
		parser->token.kind = COMMAND_NAME_RIGHT_BRACE;
		parser->cursor++;
		return;
	case ',':
		parser->token.kind = COMMAND_NAME_COMMA;
		parser->cursor++;
		return;
	default:
		break;
	}

	if (!name_character(*start, true)) {
		parser->token.kind = COMMAND_NAME_INVALID;
		parser->cursor++;
		return;
	}
	parser->cursor++;
	while (name_character(*parser->cursor, false))
		parser->cursor++;
	parser->token.kind = COMMAND_NAME_IDENT;
	parser->token.length = (size_t)(parser->cursor - start);
}

static char *
join_text(const char *left, const char *right, size_t right_length)
{
	size_t left_length = strlen(left);
	char *joined;

	if (right_length > SIZE_MAX - left_length - 1)
		return (NULL);
	joined = malloc(left_length + right_length + 1);
	if (joined == NULL)
		return (NULL);
	(void) memcpy(joined, left, left_length);
	(void) memcpy(joined + left_length, right, right_length);
	joined[left_length + right_length] = '\0';
	return (joined);
}

static bool
add_name(struct command_name_parser *parser, const char *name)
{
	struct command_name_list *list = parser->list;
	char **names;
	char *copy;
	size_t capacity;

	if (list->count == parser->capacity) {
		capacity = parser->capacity == 0 ? 8 : parser->capacity * 2;
		if (capacity < parser->capacity ||
		    capacity > SIZE_MAX / sizeof (*names))
			return (set_error(parser, "too many command data names"));
		names = realloc(list->names, capacity * sizeof (*names));
		if (names == NULL)
			return (set_error(parser, "out of memory"));
		list->names = names;
		parser->capacity = capacity;
	}
	copy = copy_text(name, strlen(name));
	if (copy == NULL)
		return (set_error(parser, "out of memory"));
	list->names[list->count++] = copy;
	return (true);
}

static bool
parse_suffix_group(struct command_name_parser *parser, const char *prefix)
{
	bool any = false;

	next_token(parser);
	while (parser->token.kind != COMMAND_NAME_RIGHT_BRACE &&
	    parser->token.kind != COMMAND_NAME_END) {
		char *name;

		if (parser->token.kind == COMMAND_NAME_COMMA) {
			next_token(parser);
			continue;
		}
		if (parser->token.kind != COMMAND_NAME_IDENT) {
			return (set_error(parser,
			    "expected data-name suffix"));
		}
		name = join_text(prefix, parser->token.text,
		    parser->token.length);
		if (name == NULL)
			return (set_error(parser, "out of memory"));
		if (!add_name(parser, name)) {
			free(name);
			return (false);
		}
		free(name);
		any = true;
		next_token(parser);
	}
	if (parser->token.kind != COMMAND_NAME_RIGHT_BRACE) {
		return (set_error(parser,
		    "expected '}' after command data-name suffix generator"));
	}
	if (!any) {
		return (set_error(parser,
		    "empty command data-name suffix generator"));
	}
	next_token(parser);
	return (true);
}

static bool
parse_path_group(struct command_name_parser *parser, const char *prefix)
{
	bool any = false;

	next_token(parser);
	while (parser->token.kind != COMMAND_NAME_RIGHT_BRACE &&
	    parser->token.kind != COMMAND_NAME_END) {
		if (parser->token.kind == COMMAND_NAME_COMMA) {
			next_token(parser);
			continue;
		}
		if (!parse_path(parser, prefix))
			return (false);
		any = true;
	}
	if (parser->token.kind != COMMAND_NAME_RIGHT_BRACE) {
		return (set_error(parser,
		    "expected '}' after command data-name generator"));
	}
	if (!any)
		return (set_error(parser, "empty command data-name generator"));
	next_token(parser);
	return (true);
}

static bool
parse_path(struct command_name_parser *parser, const char *prefix)
{
	char *path;

	if (parser->token.kind != COMMAND_NAME_IDENT) {
		return (set_error(parser,
		    "expected data-name component"));
	}
	path = join_text(prefix, parser->token.text, parser->token.length);
	if (path == NULL)
		return (set_error(parser, "out of memory"));
	next_token(parser);

	if (parser->token.kind == COMMAND_NAME_LEFT_BRACE) {
		bool result = parse_suffix_group(parser, path);

		free(path);
		return (result);
	}
	if (parser->token.kind == COMMAND_NAME_DOT) {
		char *nested = join_text(path, ".", 1);
		bool result;

		free(path);
		if (nested == NULL)
			return (set_error(parser, "out of memory"));
		next_token(parser);
		if (parser->token.kind == COMMAND_NAME_LEFT_BRACE)
			result = parse_path_group(parser, nested);
		else
			result = parse_path(parser, nested);
		free(nested);
		return (result);
	}
	if (!add_name(parser, path)) {
		free(path);
		return (false);
	}
	free(path);
	return (true);
}

static bool
parse_name(struct command_name_parser *parser)
{
	char *base;

	if (parser->token.kind != COMMAND_NAME_IDENT)
		return (set_error(parser, "expected command data name"));
	base = copy_text(parser->token.text, parser->token.length);
	if (base == NULL)
		return (set_error(parser, "out of memory"));
	next_token(parser);

	if (parser->token.kind == COMMAND_NAME_TYPE_SEPARATOR ||
	    parser->token.kind == COMMAND_NAME_DOT) {
		const char *separator =
		    parser->token.kind == COMMAND_NAME_TYPE_SEPARATOR ?
		    "::" : ".";
		char *prefix = join_text(base, separator, strlen(separator));
		bool result;

		free(base);
		if (prefix == NULL)
			return (set_error(parser, "out of memory"));
		next_token(parser);
		if (parser->token.kind == COMMAND_NAME_LEFT_BRACE)
			result = parse_path_group(parser, prefix);
		else
			result = parse_path(parser, prefix);
		free(prefix);
		return (result);
	}
	if (parser->token.kind == COMMAND_NAME_LEFT_BRACE) {
		bool result = parse_suffix_group(parser, base);

		free(base);
		return (result);
	}
	if (!add_name(parser, base)) {
		free(base);
		return (false);
	}
	free(base);
	return (true);
}

static char *
join_arguments(size_t argc, const char *const *argv)
{
	size_t length = 0;
	size_t i;
	char *text;
	char *cursor;

	for (i = 0; i < argc; i++) {
		size_t argument_length = strlen(argv[i]);

		if (argument_length > SIZE_MAX - length - 2)
			return (NULL);
		length += argument_length + (i != 0 ? 1 : 0);
	}
	text = malloc(length + 1);
	if (text == NULL)
		return (NULL);
	cursor = text;
	for (i = 0; i < argc; i++) {
		size_t argument_length = strlen(argv[i]);

		if (i != 0)
			*cursor++ = ' ';
		(void) memcpy(cursor, argv[i], argument_length);
		cursor += argument_length;
	}
	*cursor = '\0';
	return (text);
}

bool
command_names_expand(size_t argc, const char *const *argv,
    struct command_name_list *list)
{
	struct command_name_parser parser = { 0 };
	char *text;
	size_t i;
	bool grouped = false;

	*list = (struct command_name_list){ 0 };
	parser.list = list;
	for (i = 0; i < argc; i++) {
		if (strchr(argv[i], '{') != NULL ||
		    strchr(argv[i], '}') != NULL ||
		    strchr(argv[i], ',') != NULL) {
			grouped = true;
			break;
		}
	}
	if (!grouped) {
		for (i = 0; i < argc; i++) {
			if (!add_name(&parser, argv[i]))
				return (false);
		}
		return (true);
	}

	text = join_arguments(argc, argv);
	if (text == NULL)
		return (set_error(&parser, "out of memory"));
	parser.cursor = text;
	next_token(&parser);
	while (parser.token.kind != COMMAND_NAME_END) {
		if (parser.token.kind == COMMAND_NAME_COMMA) {
			next_token(&parser);
			continue;
		}
		if (!parse_name(&parser)) {
			free(text);
			return (false);
		}
	}
	free(text);
	return (true);
}

void
command_names_free(struct command_name_list *list)
{
	size_t i;

	for (i = 0; i < list->count; i++)
		free(list->names[i]);
	free(list->names);
	*list = (struct command_name_list){ 0 };
}
