/*
 *	PROGRAM:	JRD Remote Interface/Server
 *	MODULE:		merge_proto.h
 *	DESCRIPTION:	Prototpe header file for merge.c
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

#ifndef REMOTE_MERGE_PROTO_H
#define REMOTE_MERGE_PROTO_H
#if (defined __cplusplus) && (defined SOLX86)
/* Who else got mixed c and C++ linkage error - let join me. KLK
*/
extern "C" {
#endif

USHORT MERGE_database_info(UCHAR *, UCHAR *, USHORT, USHORT,
							USHORT, USHORT, UCHAR *, UCHAR *, ULONG);

#if (defined __cplusplus) && (defined SOLX86)
} // extern "C"
#endif

#endif // REMOTE_MERGE_PROTO_H
