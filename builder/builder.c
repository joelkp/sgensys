/* ssndgen: Audio program builder module.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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

#include "../ssndgen.h"
#include "../script.h"
#include "../reader/file.h"

/*
 * Open file for script arg.
 *
 * \return instance or NULL on error
 */
static SSG_File *open_file(const char *restrict script_arg, bool is_path) {
	SSG_File *f = SSG_create_File();
	if (!f) return NULL;
	if (!is_path) {
		SSG_File_stropenrb(f, "<string>", script_arg);
		return f;
	}
	if (!SSG_File_fopenrb(f, script_arg)) {
		SSG_error(NULL,
"couldn't open script file \"%s\" for reading", script_arg);
		SSG_destroy_File(f);
		return NULL;
	}
	return f;
}

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static SSG_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SSG_File *f = open_file(script_arg, is_path);
	if (!f) return NULL;

	SSG_Program *o = NULL;
	SSG_Script *sd = SSG_load_Script(f);
	if (!sd) goto CLOSE;
	o = SSG_build_Program(sd);
	SSG_discard_Script(sd);
CLOSE:
	SSG_destroy_File(f);
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
		SSG_discard_Program(prgs[i]);
	}
	SSG_PtrArr_clear(prg_objs);
}
