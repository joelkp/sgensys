/* ssndgen: Test program for experimental reader code.
 * Copyright (c) 2017-2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "ssndgen.h"
#if SSG_TEST_SCANNER
# include "reader/scanner.h"
#else
# include "reader/lexer.h"
#endif
#include "reader/file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define NAME "test-scan"

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: "NAME" [-c] [-p] [-e] <script>...\n"
"\n"
"  -e \tEvaluate strings instead of files.\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
	(by_arg) ? stdout : stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(NAME" ("SSG_CLINAME_STR") "SSG_VERSION_STR);
}

/*
 * Parse command line arguments.
 *
 * Print usage instructions if requested or args invalid.
 *
 * \return true if args valid and script path set
 */
static bool parse_args(int argc, char **restrict argv,
		uint32_t *restrict flags,
		SSG_PtrArr *restrict script_args) {
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_args->count) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			SSG_PtrArr_add(script_args, (void*) arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'c':
			if ((*flags & SSG_ARG_MODE_FULL) != 0)
				goto INVALID;
			*flags |= SSG_ARG_MODE_CHECK;
			break;
		case 'e':
			*flags |= SSG_ARG_EVAL_STRING;
			break;
		case 'h':
			if (*flags != 0) goto INVALID;
			print_usage(true);
			goto CLEAR;
		case 'p':
			*flags |= SSG_ARG_PRINT_INFO;
			break;
		case 'v':
			print_version();
			goto CLEAR;
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (script_args->count != 0);
INVALID:
	print_usage(false);
CLEAR:
	SSG_PtrArr_clear(script_args);
	return false;
}

/*
 * Run script through test code.
 *
 * \return SSG_Program or NULL on error
 */
static SSG_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SSG_Program *o = NULL;
	SSG_MemPool *mempool = SSG_create_MemPool(0);
	SSG_SymTab *symtab = SSG_create_SymTab(mempool);
	if (!symtab) return NULL;
#if SSG_TEST_SCANNER
	SSG_Scanner *scanner = SSG_create_Scanner(symtab);
	if (!scanner) goto CLOSE;
	if (!SSG_Scanner_open(scanner, script_arg, is_path)) goto CLOSE;
	for (;;) {
		uint8_t c = SSG_Scanner_getc(scanner);
		if (!c) {
			putchar('\n');
			break;
		}
		putchar(c);
	}
	o = (SSG_Program*) calloc(1, sizeof(SSG_Program)); // placeholder
CLOSE:
	if (scanner) SSG_destroy_Scanner(scanner);
#else
	SSG_Lexer *lexer = SSG_create_Lexer(symtab);
	if (!lexer) goto CLOSE;
	if (!SSG_Lexer_open(lexer, script_arg, is_path)) goto CLOSE;
	for (;;) {
		SSG_ScriptToken token;
		if (!SSG_Lexer_get(lexer, &token)) break;
	}
	o = (SSG_Program*) calloc(1, sizeof(SSG_Program)); // placeholder
CLOSE:
	if (lexer) SSG_destroy_Lexer(lexer);
#endif
	SSG_destroy_SymTab(symtab);
	SSG_destroy_MemPool(mempool);
	return o;
}

/**
 * Build the listed scripts, adding each result (even if NULL)
 * to the program list.
 *
 * \return number of programs successfully built
 */
size_t SSG_build(const SSG_PtrArr *restrict script_args, uint32_t options,
		SSG_PtrArr *restrict prg_objs) {
	bool are_paths = !(options & SSG_ARG_EVAL_STRING);
	size_t built = 0;
	const char **args = (const char**) SSG_PtrArr_ITEMS(script_args);
	for (size_t i = 0; i < script_args->count; ++i) {
		SSG_Program *prg = build_program(args[i], are_paths);
		if (prg != NULL) ++built;
		SSG_PtrArr_add(prg_objs, prg);
	}
	return built;
}

/**
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
void SSG_discard(SSG_PtrArr *restrict prg_objs) {
	SSG_Program **prgs = (SSG_Program**) SSG_PtrArr_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		free(prgs[i]); // for placeholder
	}
	SSG_PtrArr_clear(prg_objs);
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SSG_PtrArr script_args = (SSG_PtrArr){0};
	SSG_PtrArr prg_objs = (SSG_PtrArr){0};
	uint32_t options = 0;
	if (!parse_args(argc, argv, &options, &script_args))
		return 0;
	bool error = !SSG_build(&script_args, options, &prg_objs);
	SSG_PtrArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		// no audio output
		SSG_discard(&prg_objs);
	}

	return 0;
}
