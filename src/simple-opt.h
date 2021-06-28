#ifndef SIMPLE_OPT_H
#define SIMPLE_OPT_H

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

/* the maximum number of options that can be passed on the cli */
#ifndef SIMPLE_OPT_MAX_ARGC
#define SIMPLE_OPT_MAX_ARGC 1024
#endif

/* the maximum allowed width for an option passed on the cli */
#ifndef SIMPLE_OPT_OPT_MAX_WIDTH
#define SIMPLE_OPT_OPT_MAX_WIDTH 512
#endif

/* the maximum allowed width for an option's argument passed on the cli */
#ifndef SIMPLE_OPT_OPT_ARG_MAX_WIDTH
#define SIMPLE_OPT_OPT_ARG_MAX_WIDTH 2048
#endif

/* an internal print buffer width for usage printing. you shouldn't have to
 * worry about this if you're sane */
#ifndef SIMPLE_OPT_PRINT_BUFFER_WIDTH
#define SIMPLE_OPT_PRINT_BUFFER_WIDTH 2048
#endif

enum simple_opt_type {
	SIMPLE_OPT_FLAG,
	SIMPLE_OPT_BOOL,
	SIMPLE_OPT_INT,
	SIMPLE_OPT_UNSIGNED,
	SIMPLE_OPT_DOUBLE,
	SIMPLE_OPT_CHAR,
	SIMPLE_OPT_STRING,
	SIMPLE_OPT_STRING_SET,
	SIMPLE_OPT_END,
};

struct simple_opt {
	enum simple_opt_type type;
	const char short_name;
	const char *long_name;
	bool arg_is_required;

	/* optional, a description of the option used for usage printing */
	const char *description;

	/* optional, a custom string describing the arg, used for usage printing */
	const char *custom_arg_string;

	/* required for type SIMPLE_OPT_STRING_SET, a NULL-terminated array of
	 * string possibilities against which an option's argument is matched */
	const char **string_set;

	/* values assigned upon successful option parse */
	bool was_seen;
	bool arg_is_stored;

	union {
		bool v_bool;
		long v_int;
		unsigned long v_unsigned;
		double v_double;
		char v_char;
		char v_string[SIMPLE_OPT_OPT_ARG_MAX_WIDTH];
		int v_string_set_idx;
	} val;
};

enum simple_opt_result_type {
	SIMPLE_OPT_RESULT_SUCCESS,
	SIMPLE_OPT_RESULT_UNRECOGNISED_OPTION,
	SIMPLE_OPT_RESULT_BAD_ARG,
	SIMPLE_OPT_RESULT_MISSING_ARG,
	SIMPLE_OPT_RESULT_OPT_ARG_TOO_LONG,
	SIMPLE_OPT_RESULT_TOO_MANY_ARGS,
	SIMPLE_OPT_RESULT_MALFORMED_OPTION_STRUCT,
};

struct simple_opt_result {
	enum simple_opt_result_type result_type;
	enum simple_opt_type option_type;
	char option_string[SIMPLE_OPT_OPT_MAX_WIDTH];
	char argument_string[SIMPLE_OPT_OPT_ARG_MAX_WIDTH];
	struct simple_opt *option;
	int argc;
	char *argv[SIMPLE_OPT_MAX_ARGC];
};

static struct simple_opt_result simple_opt_parse(int argc, char **argv,
		struct simple_opt *options);

static void simple_opt_print_usage(FILE *f, unsigned width,
		char *command_name, char *command_options, char *command_summary,
		struct simple_opt *options);

static void simple_opt_print_error(FILE *f, unsigned width, char *command_name,
		struct simple_opt_result result);


/* 
 * internal definitions
 *
 */

static bool sub_simple_opt_parse(struct simple_opt *o, char *s)
{
	unsigned i, j;
	char *str, *cp;
	bool match;

	switch (o->type) {
	case SIMPLE_OPT_BOOL:
		goto loop;
strmatch:
		for (j = 0; j < strlen(str); j++) {
			if (s[j] == '\0' || tolower(s[j]) != str[j]) {
				match = false;
				goto strmatch_out;
			}
		}

		match = true;
		goto strmatch_out;
loop:
		for (i = 0; i < 6; i++) {
			switch (i) {
			case 0:
				str = "true";
				goto strmatch;
			case 1:
				str = "yes";
				goto strmatch;
			case 2:
				str = "on";
				goto strmatch;
			case 3:
				str = "false";
				goto strmatch;
			case 4:
				str = "no";
				goto strmatch;
			case 5:
				str = "off";
				goto strmatch;
			}
strmatch_out:
			if (match) {
				if (i < 3)
					o->val.v_bool = true;
				else
					o->val.v_bool = false;

				return true;
			}
		}

		return false;


	case SIMPLE_OPT_INT:
		errno = 0;
		o->val.v_int = strtol(s, &cp, 0);

		if (cp == s || *cp != '\0' || errno)
			return false;

		return true;

	case SIMPLE_OPT_UNSIGNED:
		if (s[0] == '-' || s[0] == '+')
			return false;

		errno = 0;
		o->val.v_unsigned = strtoul(s, &cp, 0);

		if (cp == s || *cp != '\0' || errno)
			return false;

		return true;

	case SIMPLE_OPT_DOUBLE:
		errno = 0;
		o->val.v_double = strtod(s, &cp);

		if (cp == s || *cp != '\0' || errno)
			return false;

		return true;

	case SIMPLE_OPT_CHAR:
		if (strlen(s) == 2 && s[0] == '\\') {
			switch (s[1]) {
			case '0':
				o->val.v_char = '\0';
				break;
			case 'a':
				o->val.v_char = '\a';
				break;
			case 'b':
				o->val.v_char = '\b';
				break;
			case 't':
				o->val.v_char = '\t';
				break;
			case 'n':
				o->val.v_char = '\n';
				break;
			case 'v':
				o->val.v_char = '\v';
				break;
			case 'f':
				o->val.v_char = '\f';
				break;
			case 'r':
				o->val.v_char = '\r';
				break;
			case '\\':
				o->val.v_char = '\\';
				break;
			default:
				return false;
			}
			return true;
		} else if (strlen(s) != 1) {
			return false;
		}

		o->val.v_char = s[0];
		return true;

	case SIMPLE_OPT_STRING:
		if (strlen(s) + 1 >= SIMPLE_OPT_OPT_ARG_MAX_WIDTH)
			return false;

		strcpy(o->val.v_string, s);
		return true;

	case SIMPLE_OPT_STRING_SET:
		for (i = 0; o->string_set[i] != NULL; i++) {
			if (!strcmp(s, o->string_set[i])) {
				o->val.v_string_set_idx = i;
				return true;
			}
		}

		return false;

	default:
		return false;
	}
}

static int sub_simple_opt_id(char *s, struct simple_opt *o)
{
	int i;
	char c;

	if (strlen(s) < 2)
		return -1;

	if (s[1] != '-') {
		if (strlen(s) > 2)
			return -1;

		for (i = 0; o[i].type != SIMPLE_OPT_END; i++) {
			if (s[1] == o[i].short_name)
				return i;
		}

		return -1;
	}

	for (i = 0; o[i].type != SIMPLE_OPT_END; i++) {
		if ( o[i].long_name != NULL && !strncmp(s + 2, o[i].long_name, strlen(o[i].long_name)) ) {
			c = s[2 + strlen(o[i].long_name)];
			if (c == '\0' || c == '=')
				return i;
		}
	}

	return -1;
}

static struct simple_opt_result simple_opt_parse(int argc, char **argv,
		struct simple_opt *options)
{
	int i, j, opt_i;
	int arg_end;
	char c;
	char *s;
	struct simple_opt_result r;

	/* check for malformed options */
	for (i = 0; options[i].type != SIMPLE_OPT_END; i++) {
		if ( (options[i].short_name == '\0' && options[i].long_name == NULL) 
				|| (options[i].type == SIMPLE_OPT_FLAG &&
					options[i].arg_is_required) 
				|| (options[i].type == SIMPLE_OPT_STRING_SET &&
					options[i].string_set == NULL) ) {
			r.result_type = SIMPLE_OPT_RESULT_MALFORMED_OPTION_STRUCT;
			goto end;
		}
	}

	/* check for duplicate options. can't modify anything so this is going to
	 * be pretty not-optimised, but ah well */
	for (i = 0; options[i].type != SIMPLE_OPT_END; i++) {
		for (j = 0; options[j].type != SIMPLE_OPT_END; j++) {
			if (i != j && (
						( options[i].short_name != '\0'
						&& options[j].short_name != '\0'
						&& options[i].short_name == options[j].short_name)
					|| ( options[i].long_name != NULL
						&& options[j].long_name != NULL
						&& !strcmp(options[i].long_name, options[j].long_name))
					)
				) {
				r.result_type = SIMPLE_OPT_RESULT_MALFORMED_OPTION_STRUCT;
				goto end;
			}
		}
	}

	r.argc = 0;

	r.result_type = SIMPLE_OPT_RESULT_SUCCESS;

	for (i = 1; i < argc; i++) {
		/* "following are non-opts" marker */
		if ( !strcmp(argv[i], "--") ) {
			i++;
			break;
		}

		/* if not an opt, add to r.argv */
		if (argv[i][0] != '-') {

			if (r.argc + 1 > SIMPLE_OPT_MAX_ARGC) {
				r.result_type = SIMPLE_OPT_RESULT_TOO_MANY_ARGS;
				goto end;
			}

			r.argv[r.argc] = argv[i];
			r.argc++;
			continue;
		}

		/* unrecognised argument */
		if (strlen(argv[i]) < 2) {
			r.result_type = SIMPLE_OPT_RESULT_UNRECOGNISED_OPTION;
			goto opt_copy_and_return;
		}

		/* identify this option */
		opt_i = sub_simple_opt_id(argv[i], options);

		if (opt_i == -1) {
			r.result_type = SIMPLE_OPT_RESULT_UNRECOGNISED_OPTION;
			goto opt_copy_and_return;
		}

		options[opt_i].was_seen = true;

		if (options[opt_i].type == SIMPLE_OPT_FLAG)
			continue;

		/* if there's an arg, is it a separate element in argv? or is it passed
		 * as "--X=arg"? */
		if (argv[i][1] == '-')
			c = argv[i][2 + strlen(options[opt_i].long_name)];
		else
			c = '\0';

		/* if this option doesn't require an arg and none is to be found,
		 * just continue */
		if (!options[opt_i].arg_is_required && c == '\0') {
			if (i + 1 >= argc)
				continue;

			if (!strcmp(argv[i+1], "--"))
				continue;

			if (sub_simple_opt_id(argv[i+1], options) != -1)
				continue;
		}

		if (c == '\0') {
			if (i + 1 >= argc) {
				r.result_type = SIMPLE_OPT_RESULT_MISSING_ARG;
				r.option_type = options[opt_i].type;
				r.option = options + opt_i;
				goto opt_copy_and_return;
			}
			s = argv[i+1];
		} else {
			if (argv[i][3 + strlen(options[opt_i].long_name)] == '\0') {
				r.result_type = SIMPLE_OPT_RESULT_MISSING_ARG;
				r.option_type = options[opt_i].type;
				r.option = options + opt_i;
				goto opt_copy_and_return;
			}

			s = argv[i] + 3 + strlen(options[opt_i].long_name);
		}

		/* is there space for the arg (if this opt wants a string)? */
		if (options[opt_i].type == SIMPLE_OPT_STRING
				&& strlen(s) + 1 >= SIMPLE_OPT_OPT_ARG_MAX_WIDTH) {
			r.result_type = SIMPLE_OPT_RESULT_OPT_ARG_TOO_LONG;
			r.option_type = options[opt_i].type;
			r.option = options + opt_i;
			goto opt_copy_and_return;
		}

		/* try to actually parse the thing */
		if (sub_simple_opt_parse(&(options[opt_i]), s) ) {
			options[opt_i].arg_is_stored = true;
			/* skip forwards in argv if this wasn't an "="-type argument
			 * passing */
			if (i + 1 < argc && s == argv[i+1])
				i++;
		} else {
			r.result_type = SIMPLE_OPT_RESULT_BAD_ARG;
			r.option_type = options[opt_i].type;
			strncpy(r.argument_string, s, SIMPLE_OPT_OPT_ARG_MAX_WIDTH - 1);
			r.argument_string[SIMPLE_OPT_OPT_ARG_MAX_WIDTH - 1] = '\0';
			r.option = options + opt_i;
			goto opt_copy_and_return;
		}

		continue;
	}

	/* copy anything that follows -- into r.argv */

	for (; i < argc; i++, r.argc++)
		r.argv[r.argc] = argv[i];

end:

	return r;

opt_copy_and_return:
	for(arg_end = 0; argv[i][arg_end] != '=' && argv[i][arg_end] != '\0';
			arg_end++);

	strncpy(r.option_string, argv[i], SIMPLE_OPT_OPT_MAX_WIDTH - 1 < arg_end ?
			SIMPLE_OPT_OPT_MAX_WIDTH - 1 : arg_end);

	r.option_string[SIMPLE_OPT_OPT_MAX_WIDTH - 1 < arg_end ?
		SIMPLE_OPT_OPT_MAX_WIDTH -1 : arg_end] = '\0';

	goto end;
}

static int sub_simple_opt_wrap_print(FILE *f, unsigned width, unsigned col,
		unsigned line_start, const char *s)
{
	bool add_newline = false, first_word = true, first_line = true;
	unsigned i, j, word_start, word_end;

	if (width != 0 && line_start >= width) {
		line_start = 0;
		add_newline = true;
	}

	if (width != 0 && col >= width)
		add_newline = true;

	if (add_newline) {
		fprintf(f, "\n");
		col = 0;
		first_line = false;
	}

	/* print out the message, trying to wrap at words */
	word_start = 0;
	while (1) {
		/* find the next word */
		while ( isspace(s[word_start]) )
			word_start++;

		/* null appeared before any non-spaces */
		if (s[word_start] == '\0')
			return col;

		word_end = word_start;
		while ( (s[word_end] != '\0') && !isspace(s[word_end]) )
			word_end++;

		/* buffer up to line_start with spaces */
		while (col < line_start + 2 * (!first_line && width > 40
					&& line_start > 5)) {
			fprintf(f, " ");
			col++;
		}

		/* if too little space left, wrap */
		if (width != 0 && col + (word_end - word_start) + (first_word ? 0 : 1)
				> width && first_word == false) {
			fprintf(f, "\n");
			first_line = false;
			/* buffer up to line_start with spaces */
			col = 0;
			while (col < line_start + 2 * (!first_line && width > 40
						&& line_start > 5)) {
				fprintf(f, " ");
				col++;
			}
			first_word = true;
		} 
		
		if (first_word == false) {
			fprintf(f, " ");
			col++;
		}

		/* if too long, print piecemeal */
		if (width != 0 && (line_start + (word_end - word_start) > width
					|| (first_word && col + (word_end - word_start) > width)) ) {
			j = word_start;
			while (1) {
				for (i = 0; col < width && j < word_end; i++, j++) {
					fprintf(f, "%c", s[j]);
					col++;
				}

				if (j == word_end)
					break;

				col = 0;
				fprintf(f, "\n");
				first_line = false;
				while (col < line_start + 2 * (!first_line && width > 40
							&& line_start > 5)) {
					fprintf(f, " ");
					col++;
				}
			}
		/* else just print and move to the next word */
		} else {
			for (i = 0; i < word_end - word_start; i++)
				fprintf(f, "%c", s[word_start + i]);

			col += i;
		}

		word_start = word_end;
		first_word = false;
	}

	return col;
}

static void simple_opt_print_usage(FILE *f, unsigned width,
		char *command_name, char *command_options, char *command_summary,
		struct simple_opt *options)
{
	char print_buffer[SIMPLE_OPT_PRINT_BUFFER_WIDTH];
	unsigned i, j, col, print_buffer_offset, desc_line_start;

	/* calculate the required line_start for printing descriptions (leaving
	 * space for the widest existing long-option) */

	/* check for space for column 1 (short_name) */
	if (5 >= SIMPLE_OPT_PRINT_BUFFER_WIDTH) {
		fprintf(f, "simple-opt internal err: print buffer too small\n");
		return;
	}

	/* 4 to start with, leaving space for "  -X " */
	desc_line_start = 5;

	/* check for space for column 2 (long_name / arg type) */
	for (i = 0; options[i].type != SIMPLE_OPT_END; i++) {
		j = 0;

		/* 3 for "--" and "=" */
		if (options[i].long_name != NULL)
			j += 3 + strlen(options[i].long_name);

		/* 2 for optional args, where arg is wrapped in [] */
		if (!options[i].arg_is_required && options[i].type != SIMPLE_OPT_FLAG)
			j += 2;

		/* the width of the arg type string (BOOL, INT, UNSIGNED, STRING etc)
		 * FLAGs don't take an argument, so 0 */
		if (options[i].type != SIMPLE_OPT_FLAG) {
			/* if there's a custom string, use that width. else, use one of the
			 * default widths */
			if (options[i].custom_arg_string != NULL) {
				j += strlen(options[i].custom_arg_string);
			} else {
				switch (options[i].type) {
				case SIMPLE_OPT_BOOL:
					j += 4;
					break;
				case SIMPLE_OPT_INT:
					j += 3;
					break;
				case SIMPLE_OPT_UNSIGNED:
					j += 8;
					break;
				case SIMPLE_OPT_DOUBLE:
					j += 6;
					break;
				case SIMPLE_OPT_CHAR:
					j += 4;
					break;
				case SIMPLE_OPT_STRING:
				case SIMPLE_OPT_STRING_SET:
					j += 6;
					break;
				default:
					break;
				}
			}
		}

		/* 5 for leading "  -X ", 2 for trailing " " */
		if (desc_line_start < j + 5 + 2)
			desc_line_start = j + 5 + 2;
	}

	/* check for space for long_name printing */
	if (desc_line_start - 5 - 2 >= SIMPLE_OPT_PRINT_BUFFER_WIDTH) {
		fprintf(f, "simple-opt internal err: usage print buffer too small\n");
		return;
	}

	/* if the desc_line_start is so far over it threatens readability, move it
	 * back a bit and just let the offending longer args be offset */
	if (desc_line_start > (width / 2 < 30 ? width / 2 : 30))
		desc_line_start = (width / 2 < 30 ? width / 2 : 30);



	/* 
	 * printing 
	 *
	 */

	/* print "Usage: <exec> <options> */
	if (command_name != NULL && command_options != NULL) {
		col = sub_simple_opt_wrap_print(f, width, 0, 0, "Usage:");

		col = sub_simple_opt_wrap_print(f, width, col, 7, command_name);

		if (command_options != NULL)
			sub_simple_opt_wrap_print(f, width, col,
					7 + strlen(command_name) + 1, command_options);

		fprintf(f, "\n\n");
	}

	/* print summary line */
	if (command_summary != NULL) {
		sub_simple_opt_wrap_print(f, width, 0, 2, command_summary);
		fprintf(f, "\n\n");
	}

	/* print option list */
	for (i = 0; options[i].type != SIMPLE_OPT_END; i++) {

		/* print column 1 (short name) */

		if (options[i].short_name != '\0') {
			if (sprintf(print_buffer, "-%c", options[i].short_name) < 0) {
				fprintf(f, "\nsimple-opt internal err: encoding error printing"
						"option %i\n", i);
				return;
			}
		} else {
			sprintf(print_buffer, "%c", '\0');
		}

		col = sub_simple_opt_wrap_print(f, width, 0, 2, print_buffer);

		/* print column 2 (long_name and type) */
		sprintf(print_buffer, "%c", '\0');
		print_buffer_offset = 0;

		if (options[i].long_name != NULL) {
			sprintf(print_buffer, "--");
			print_buffer_offset = 2;

			if (sprintf(print_buffer + print_buffer_offset, "%s",
						options[i].long_name) < 0) {
				fprintf(f, "\nsimple-opt internal err: encoding error printing"
						"option %i\n", i);
				return;
			}
			print_buffer_offset += strlen(options[i].long_name);
		}

		if (!options[i].arg_is_required && options[i].type !=
				SIMPLE_OPT_FLAG) {
			sprintf(print_buffer + print_buffer_offset, "[");
			print_buffer_offset++;
		}

		if (options[i].long_name != NULL && options[i].type != SIMPLE_OPT_FLAG) {
			sprintf(print_buffer + print_buffer_offset, "=");
			print_buffer_offset++;
		}

		if (options[i].type != SIMPLE_OPT_FLAG) {
			if (options[i].custom_arg_string != NULL) {
				sprintf(print_buffer + print_buffer_offset,
						"%s", options[i].custom_arg_string);
				print_buffer_offset += strlen(options[i].custom_arg_string);
			} else {
				switch (options[i].type) {
				case SIMPLE_OPT_BOOL:
					sprintf(print_buffer + print_buffer_offset, "BOOL");
					print_buffer_offset += 4;
					break;
				case SIMPLE_OPT_INT:
					sprintf(print_buffer + print_buffer_offset, "INT");
					print_buffer_offset += 3;
					break;
				case SIMPLE_OPT_UNSIGNED:
					sprintf(print_buffer + print_buffer_offset, "UNSIGNED");
					print_buffer_offset += 8;
					break;
				case SIMPLE_OPT_DOUBLE:
					sprintf(print_buffer + print_buffer_offset, "DOUBLE");
					print_buffer_offset += 6;
					break;
				case SIMPLE_OPT_CHAR:
					sprintf(print_buffer + print_buffer_offset, "CHAR");
					print_buffer_offset += 4;
					break;
				case SIMPLE_OPT_STRING:
				case SIMPLE_OPT_STRING_SET:
					sprintf(print_buffer + print_buffer_offset, "STRING");
					print_buffer_offset += 6;
					break;
				default:
					break;
				}
			}
		}

		if (!options[i].arg_is_required && options[i].type !=
				SIMPLE_OPT_FLAG)
			sprintf(print_buffer + print_buffer_offset, "]");

		/* 5 for "  -X --" */
		col = sub_simple_opt_wrap_print(f, width, col, 5, print_buffer);

		/* print option description */
		if (options[i].description != NULL) {
			if (col < width) {
				fprintf(f, " ");
				col++;
			}
			if (col < width) {
				fprintf(f, " ");
				col++;
			}
			sub_simple_opt_wrap_print(f, width, col, desc_line_start,
					options[i].description);
		}

		/* end of this option */
		fprintf(f, "\n");
	}
}

static void simple_opt_print_error(FILE *f, unsigned width, char *command_name,
		struct simple_opt_result result)
{
	char print_buffer[SIMPLE_OPT_PRINT_BUFFER_WIDTH];
	unsigned i, line_start, col;
	int rval;

	/* just easier to write */
	const size_t size = SIMPLE_OPT_PRINT_BUFFER_WIDTH;

	if (result.result_type == SIMPLE_OPT_RESULT_SUCCESS)
		return;

	if (command_name != NULL)
		rval = snprintf(print_buffer, size, "%s:", command_name);
	else
		rval = snprintf(print_buffer, size, "err:");

	if (rval < 0 || (unsigned)rval >= size) {
		fprintf(f, "simple-opt internal err: print buffer too small\n");
		return;
	}

	col = sub_simple_opt_wrap_print(f, width, 0, 0, print_buffer);

	line_start = strlen(print_buffer) + 1;

	switch (result.result_type) {
	case SIMPLE_OPT_RESULT_UNRECOGNISED_OPTION:
		rval = snprintf(print_buffer, size, "unrecognised option `%s`",
				result.option_string);
		break;

	case SIMPLE_OPT_RESULT_BAD_ARG:
		rval = snprintf(print_buffer, size,
				"bad argument `%s` passed to option `%s`",
				result.argument_string, result.option_string);

		if (rval < 0 || (unsigned)rval >= size) {
			fprintf(f, "simple-opt internal err: print buffer too small\n");
			return;
		}

		sub_simple_opt_wrap_print(f, width, col, line_start,
				print_buffer);
		fprintf(f, "\n");
		col = 0;

		switch (result.option_type) {
		case SIMPLE_OPT_BOOL:
			rval = snprintf(print_buffer, size,
					"expected a boolean: (yes|true|on) or (no|false|off)");
			break;
		case SIMPLE_OPT_INT:
			rval = snprintf(print_buffer, size,"expected integer value");
			break;
		case SIMPLE_OPT_UNSIGNED:
			rval = snprintf(print_buffer, size,
					"expected unsigned integer value");
			break;
		case SIMPLE_OPT_DOUBLE:
			rval = snprintf(print_buffer, size,
					"expected floating-point value");
			break;
		case SIMPLE_OPT_CHAR:
			rval = snprintf(print_buffer, size,
					"expected single character");
			break;
		case SIMPLE_OPT_STRING:
			rval = snprintf(print_buffer, size,
					"expected a string");
			break;
		case SIMPLE_OPT_STRING_SET:
			for (i = 0; result.option->string_set[i] != NULL; i++);
			if (i == 1) {
				rval = snprintf(print_buffer, size, "expected \"%s\"",
						result.option->string_set[0]);
			} else if (i == 2) {
				rval = snprintf(print_buffer, size,
						"expected \"%s\" or \"%s\"",
						result.option->string_set[0],
						result.option->string_set[1]);
			} else if (i == 3) {
				rval = snprintf(print_buffer, size,
						"expected \"%s\", \"%s\" or \"%s\"",
						result.option->string_set[0],
						result.option->string_set[1],
						result.option->string_set[2]);
			} else if (i == 4) {
				rval = snprintf(print_buffer, size,
						"expected \"%s\", \"%s\", \"%s\", or \"%s\"",
						result.option->string_set[0],
						result.option->string_set[1],
						result.option->string_set[2],
						result.option->string_set[3]);
			} else {
				rval = snprintf(print_buffer, size,
						"expected one of %u possibile strings", i);
			}
		default:
			break;
		}
		break;

	case SIMPLE_OPT_RESULT_MISSING_ARG:
		rval = snprintf(print_buffer, size,
				"argument expected for option `%s`",
				result.option_string);
		break;

	case SIMPLE_OPT_RESULT_OPT_ARG_TOO_LONG:
		rval = snprintf(print_buffer, size,
				"argument passed to option `%s` is too long",
				result.option_string);
		break;

	case SIMPLE_OPT_RESULT_TOO_MANY_ARGS:
		rval = snprintf(print_buffer, size,
				"too many cli arguments received");
		break;

	case SIMPLE_OPT_RESULT_MALFORMED_OPTION_STRUCT:
		rval = snprintf(print_buffer, size,
				"malformed option struct (internal err)");
		break;

	default:
		break;
	}

	if (rval < 0 || (unsigned)rval >= size) {
		fprintf(f, "simple-opt internal err: print buffer too small");
		return;
	}

	sub_simple_opt_wrap_print(f, width, col, line_start, print_buffer);
	fprintf(f, "\n");
}

#endif
