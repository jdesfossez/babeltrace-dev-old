/*
 * Common Trace Format
 *
 * Sequence format access functions.
 *
 * Copyright 2010-2011 EfficiOS Inc. and Linux Foundation
 *
 * Author: Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include <babeltrace/ctf-text/types.h>
#include <stdio.h>

int ctf_text_sequence_write(struct stream_pos *ppos, struct definition *definition)
{
	struct ctf_text_stream_pos *pos = ctf_text_pos(ppos);
	struct definition_sequence *sequence_definition =
		container_of(definition, struct definition_sequence, p);
	struct declaration_sequence *sequence_declaration =
		sequence_definition->declaration;
	struct declaration *elem = sequence_declaration->elem;
	int field_nr_saved;
	int ret = 0;

	if (!print_field(definition))
		return 0;

	if (!pos->dummy) {
		if (pos->field_nr++ != 0)
			fprintf(pos->fp, ",");
		fprintf(pos->fp, " ");
		if (pos->print_names)
			fprintf(pos->fp, "%s = ",
				g_quark_to_string(definition->name));
	}

	if (elem->id == CTF_TYPE_INTEGER) {
		struct declaration_integer *integer_declaration =
			container_of(elem, struct declaration_integer, p);

		if (integer_declaration->encoding == CTF_STRING_UTF8
		      || integer_declaration->encoding == CTF_STRING_ASCII) {

			if (!(integer_declaration->len == CHAR_BIT
			    && integer_declaration->p.alignment == CHAR_BIT)) {
				pos->string = sequence_definition->string;
				g_string_assign(sequence_definition->string, "");
				ret = sequence_rw(ppos, definition);
				pos->string = NULL;
			}
			fprintf(pos->fp, "\"%s\"", sequence_definition->string->str);
			return ret;
		}
	}

	if (!pos->dummy) {
		fprintf(pos->fp, "[");
		pos->depth++;
	}
	field_nr_saved = pos->field_nr;
	pos->field_nr = 0;
	ret = sequence_rw(ppos, definition);
	if (!pos->dummy) {
		pos->depth--;
		fprintf(pos->fp, " ]");
	}
	pos->field_nr = field_nr_saved;
	return ret;
}
