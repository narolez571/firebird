/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		err.c
 *	DESCRIPTION:	Error handlers
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
 * 
 * 27 Nov 2001  Ann W. Harrison - preserve string arguments in
 *              ERRD_post_warning
 *
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "MPEXL" port
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 */

#include "firebird.h"
#include "../jrd/ib_stdio.h"
#include <string.h>
#include "../jrd/common.h"
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#include "../dsql/dsql.h"
#include "../dsql/sqlda.h"
#include "gen/codes.h"
#include "../jrd/iberr.h"
#include "../dsql/errd_proto.h"
#include "../dsql/utld_proto.h"
#include "../jrd/err_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/thd_proto.h"


extern "C" {


ASSERT_FILENAME					/* Define things assert() needs */
#ifdef DEV_BUILD
void ERRD_assert_msg(const char* msg, const char* file, ULONG lineno)
{
/**************************************
 *
 *	E R R D _ a s s e r t _ m s g
 *
 **************************************
 *
 * Functional description
 *	Generate an assertion failure with a message
 *
 **************************************/

	char buffer[100];

	sprintf(buffer, "Assertion failure: %s File: %s Line: %ld\n",	/* NTX: dev build */
			(msg ? msg : ""), (file ? file : ""), lineno);
	ERRD_bugcheck(buffer);
}
#endif /* DEV_BUILD */


void ERRD_bugcheck(const char* text)
{
/**************************************
 *
 *	E R R D _ b u g c h e c k
 *
 **************************************
 *
 * Functional description
 *	Somebody has screwed up.  Bugcheck.
 *
 **************************************/
	TEXT s[128];

	sprintf(s, "INTERNAL: %s", text);	/* TXNN */
	ERRD_error(-1, s);
}


void ERRD_error( int code, const char* text)
{
/**************************************
 *
 *	E R R D _ e r r o r
 *
 **************************************
 *
 * Functional description
 *	This routine should only be used by fatal
 *	error messages, those that cannot use the
 *	normal error routines because something
 *	is very badly wrong.  ERRD_post() should
 * 	be used by most error messages, especially
 *	so that strings will be handled.
 *
 **************************************/
	TEXT s[256];
	TSQL tdsql;
    STATUS	*status_vector;

	tdsql = GET_THREAD_DATA;

	sprintf(s, "** DSQL error: %s **\n", text);
	TRACE(s);

    if (status_vector = tdsql->tsql_status) {
        *status_vector++ = gds_arg_gds;
        *status_vector++ = gds_random;
        *status_vector++ = gds_arg_cstring;
        *status_vector++ = strlen(s);
        *status_vector++ = reinterpret_cast<long>(s);
        *status_vector++ = gds_arg_end;
    }

    ERRD_punt();

}


BOOLEAN ERRD_post_warning(STATUS status, ...)
{
/**************************************
 *
 *      E R R D _ p o s t _ w a r n i n g
 *
 **************************************
 *
 * Functional description
 *      Post a warning to the current status vector.
 *
 **************************************/
	va_list args;

#pragma FB_COMPILER_MESSAGE("Warning, using STATUS array to hold pointers to STATUSes!")
// meaning; if sizeof(long) != sizeof(void*), this code WILL crash something.


	VA_START(args, status);

	STATUS* status_vector = ((TSQL) GET_THREAD_DATA)->tsql_status;
	int indx = 0;

	if (status_vector[0] != gds_arg_gds ||
		(status_vector[0] == gds_arg_gds && status_vector[1] == 0 &&
		 status_vector[2] != gds_arg_warning))
	{
		/* this is a blank status vector */
		status_vector[0] = gds_arg_gds;
		status_vector[1] = 0;
		status_vector[2] = gds_arg_end;
		indx = 2;
	}
	else
	{
		/* find end of a status vector */
		int warning_indx = 0;
		PARSE_STATUS(status_vector, indx, warning_indx);
		if (indx) {
			--indx;
		}
	}

/* stuff the warning */
	if (indx + 3 >= ISC_STATUS_LENGTH)
	{
		/* not enough free space */
		return FALSE;
	}

	status_vector[indx++] = gds_arg_warning;
	status_vector[indx++] = status;
	int type, len;
	while ((type = va_arg(args, int)) && (indx + 3 < ISC_STATUS_LENGTH))
	{

        char* pszTmp = NULL;
		switch (status_vector[indx++] = type)
		{
		case gds_arg_warning:
			status_vector[indx++] = (STATUS) va_arg(args, STATUS);
			break;

		case gds_arg_string: 
            pszTmp = va_arg(args, char*);
            if (strlen(pszTmp) >= MAX_ERRSTR_LEN) {
                status_vector[(indx - 1)] = gds_arg_cstring;
                status_vector[indx++] = MAX_ERRSTR_LEN;
            }
            status_vector[indx++] = reinterpret_cast<long>(ERR_cstring(pszTmp));
			break;

		case gds_arg_interpreted: 
            pszTmp = va_arg(args, char*);
            status_vector[indx++] = reinterpret_cast<long>(ERR_cstring(pszTmp));
			break;

		case gds_arg_cstring:
            len = va_arg(args, int);
            status_vector[indx++] =
                (STATUS) (len >= MAX_ERRSTR_LEN) ? MAX_ERRSTR_LEN : len;
            pszTmp = va_arg(args, char*);
            status_vector[indx++] = reinterpret_cast<long>(ERR_cstring(pszTmp));
			break;

		case gds_arg_number:
			status_vector[indx++] = (STATUS) va_arg(args, SLONG);
			break;

		case gds_arg_vms:
		case gds_arg_unix:
		case gds_arg_win32:
		default:
			status_vector[indx++] = (STATUS) va_arg(args, int);
			break;
		}
    }
	status_vector[indx] = gds_arg_end;
	return TRUE;
}


void ERRD_post(STATUS status, ...)
{
/**************************************
 *
 *	E R R D _ p o s t
 *
 **************************************
 *
 * Functional description
 *	Post an error, copying any potentially
 *	transient data before we punt.
 *
 **************************************/

	STATUS tmp_status[ISC_STATUS_LENGTH];
	STATUS warning_status[ISC_STATUS_LENGTH];
	int tmp_status_len = 0;
	int status_len = 0;
	int warning_indx = 0;

	STATUS*status_vector = ((TSQL) GET_THREAD_DATA)->tsql_status;

/* stuff the status into temp buffer */
	MOVE_CLEAR(tmp_status, sizeof(tmp_status));
	STUFF_STATUS(tmp_status, status);

/* calculate length of the status */
	PARSE_STATUS(tmp_status, tmp_status_len, warning_indx);
	assert(warning_indx == 0);

	if (status_vector[0] != gds_arg_gds ||
		(status_vector[0] == gds_arg_gds && status_vector[1] == 0 &&
		 status_vector[2] != gds_arg_warning))
	{
		/* this is a blank status vector */
		status_vector[0] = gds_arg_gds;
		status_vector[1] = gds_dsql_error;
		status_vector[2] = gds_arg_end;
	}

	PARSE_STATUS(status_vector, status_len, warning_indx);
	if (status_len)
		--status_len;

	// check for duplicated error code
	int i;
	for (i = 0; i < ISC_STATUS_LENGTH; i++)
	{
		if (status_vector[i] == gds_arg_end && i == status_len) {
			break;				/* end of argument list */
		}

		if (i && i == warning_indx) {
			break;				/* vector has no more errors */
		}

		if (status_vector[i] == tmp_status[1] && i &&
			status_vector[i - 1] != gds_arg_warning &&
			i + tmp_status_len - 2 < ISC_STATUS_LENGTH &&
			(memcmp(&status_vector[i], &tmp_status[1],
					sizeof(STATUS) * (tmp_status_len - 2)) == 0))
		{
			/* duplicate found */
			ERRD_punt();
		}
	}

	// if the status_vector has only warnings then adjust err_status_len
	int err_status_len = i;
	if (err_status_len == 2 && warning_indx) {
		err_status_len = 0;
	}

	int warning_count = 0;

	if (warning_indx) {
		/* copy current warning(s) to a temp buffer */
		MOVE_CLEAR(warning_status, sizeof(warning_status));
		MOVE_FASTER(&status_vector[warning_indx], warning_status,
					sizeof(STATUS) * (ISC_STATUS_LENGTH - warning_indx));
		PARSE_STATUS(warning_status, warning_count, warning_indx);
	}

	// add the status into a real buffer right in between last
	// error and first warning

	i = err_status_len + tmp_status_len;
	if (i < ISC_STATUS_LENGTH)
	{
		MOVE_FASTER(tmp_status, &status_vector[err_status_len],
					sizeof(STATUS) * tmp_status_len);
		/* copy current warning(s) to the status_vector */
		if (warning_count && i + warning_count - 1 < ISC_STATUS_LENGTH)
		{
			MOVE_FASTER(warning_status, &status_vector[i - 1],
						sizeof(STATUS) * warning_count);

		}
	}
	ERRD_punt();
}


void ERRD_punt(void)
{
/**************************************
 *
 *	E R R D _ p u n t
 *
 **************************************
 *
 * Functional description
 *	Error stuff has been copied to
 *	status vector.  Now punt.
 *
 **************************************/
	TSQL tdsql;

	tdsql = GET_THREAD_DATA;

/* Save any strings in a permanent location */

	UTLD_save_status_strings(tdsql->tsql_status);

/* Give up whatever we were doing and return to the user. */

	Firebird::status_exception::raise(tdsql->tsql_status[1]);
}


}	// extern "C"
