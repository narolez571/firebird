/*
 *	PROGRAM:	Preprocessor
 *	MODULE:		par_proto.h
 *	DESCRIPTION:	Prototype header file for par.c
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#ifndef GPRE_PAR_PROTO_H
#define GPRE_PAR_PROTO_H

ACT		PAR_action(TEXT*);
SSHORT	PAR_blob_subtype(DBB);
ACT		PAR_database(bool, const TEXT*);
bool	PAR_end(void);
void	PAR_error(const TEXT *);
ACT		PAR_event_init(bool);
ACT		PAR_event_wait(bool);
void	PAR_fini(void);
TOK		PAR_get_token(void);
void	PAR_init(void);
TEXT*	PAR_native_value(bool, bool);
GPRE_FLD PAR_null_field(void);
void	PAR_reserving(USHORT, bool);
GPRE_REQ PAR_set_up_dpb_info(RDY, ACT, USHORT);
SYM		PAR_symbol(enum sym_t);
void	PAR_unwind(void);
void	PAR_using_db(void);

#endif // GPRE_PAR_PROTO_H
