/* saugns: Test program for experimental reader code.
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

#include "saugns.h"
#if SAU_TEST_SCANNER
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
static void print_usage(bool h_arg sauMaybeUnused,
		const char *restrict h_type sauMaybeUnused) {
	fputs(
"Usage: "NAME" [-c] [-p] [-e] <script>...\n"
"\n"
"  -e \tEvaluate strings instead of files.\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
		stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(NAME" ("SAU_CLINAME_STR") "SAU_VERSION_STR);
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
		SAU_PtrArr *restrict script_args) {
	bool h_arg = false;
	const char *h_type = NULL;
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_args->count) goto USAGE;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			SAU_PtrArr_add(script_args, (void*) arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'c':
			if ((*flags & SAU_ARG_MODE_FULL) != 0)
				goto USAGE;
			*flags |= SAU_ARG_MODE_CHECK;
			break;
		case 'e':
			*flags |= SAU_ARG_EVAL_STRING;
			break;
		case 'h':
			h_arg = true;
			if (arg[1] != '\0') goto USAGE;
			if (*flags != 0) goto USAGE;
			--argc;
			++argv;
			if (argc < 1) goto USAGE;
			arg = *argv;
			h_type = arg;
			goto USAGE;
		case 'p':
			*flags |= SAU_ARG_PRINT_INFO;
			break;
		case 'v':
			print_version();
			goto CLEAR;
		default:
			goto USAGE;
		}
		goto NEXT_C;
	}
	return (script_args->count != 0);
USAGE:
	print_usage(h_arg, h_type);
CLEAR:
	SAU_PtrArr_clear(script_args);
	return false;
}

/*
 * Run script through test code.
 *
 * \return SAU_Program or NULL on error
 */
static SAU_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SAU_Program *o = NULL;
	SAU_MemPool *mempool = SAU_create_MemPool(0);
	SAU_SymTab *symtab = SAU_create_SymTab(mempool);
	if (!symtab)
		return NULL;
#if SAU_TEST_SCANNER
	SAU_Scanner *scanner = SAU_create_Scanner(symtab);
	if (!scanner) goto CLOSE;
	if (!SAU_Scanner_open(scanner, script_arg, is_path)) goto CLOSE;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(scanner);
		if (!c) {
			putchar('\n');
			break;
		}
		putchar(c);
	}
	o = (SAU_Program*) calloc(1, sizeof(SAU_Program)); // placeholder
CLOSE:
	SAU_destroy_Scanner(scanner);
#else
	SAU_Lexer *lexer = SAU_create_Lexer(symtab);
	if (!lexer) goto CLOSE;
	if (!SAU_Lexer_open(lexer, script_arg, is_path)) goto CLOSE;
	for (;;) {
		SAU_ScriptToken token;
		if (!SAU_Lexer_get(lexer, &token)) break;
	}
	o = (SAU_Program*) calloc(1, sizeof(SAU_Program)); // placeholder
CLOSE:
	SAU_destroy_Lexer(lexer);
#endif
	SAU_destroy_SymTab(symtab);
	SAU_destroy_MemPool(mempool);
	return o;
}

/**
 * Build the listed scripts, adding each result (even if NULL)
 * to the program list.
 *
 * \return number of programs successfully built
 */
size_t SAU_build(const SAU_PtrArr *restrict script_args, uint32_t options,
		SAU_PtrArr *restrict prg_objs) {
	bool are_paths = !(options & SAU_ARG_EVAL_STRING);
	size_t built = 0;
	const char **args = (const char**) SAU_PtrArr_ITEMS(script_args);
	for (size_t i = 0; i < script_args->count; ++i) {
		SAU_Program *prg = build_program(args[i], are_paths);
		if (prg != NULL) ++built;
		SAU_PtrArr_add(prg_objs, prg);
	}
	return built;
}

/**
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
void SAU_discard(SAU_PtrArr *restrict prg_objs) {
	SAU_Program **prgs = (SAU_Program**) SAU_PtrArr_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		free(prgs[i]); // for placeholder
	}
	SAU_PtrArr_clear(prg_objs);
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SAU_PtrArr script_args = (SAU_PtrArr){0};
	SAU_PtrArr prg_objs = (SAU_PtrArr){0};
	uint32_t options = 0;
	if (!parse_args(argc, argv, &options, &script_args))
		return 0;
	bool error = !SAU_build(&script_args, options, &prg_objs);
	SAU_PtrArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		// no audio output
		SAU_discard(&prg_objs);
	}
	return 0;
}
