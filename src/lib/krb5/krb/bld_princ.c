/*
 * $Source$
 * $Author$
 *
 * Copyright 1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * Build a principal from a list of strings
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_bld_princ_c [] =
"$Id$";
#endif	/* !lint & !SABER */

/* Need <krb5/config.h> for STDARG_PROTOTYPES */
#include <krb5/krb5.h>

#if __STDC__ || defined(STDARG_PROTOTYPES)
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#include <krb5/ext-proto.h>

krb5_error_code
krb5_build_principal_va(princ, rlen, realm, ap)
krb5_principal princ;
int rlen;
const char *realm;
va_list ap;
{
    register int i, count = 0;
    register char *next;
    char *tmpdata;
    krb5_data *data;

    /* guess at an initial sufficent count of 2 pieces */
    count = 2;

    /* get space for array and realm, and insert realm */
    data = (krb5_data *) malloc(sizeof(krb5_data) * count);
    if (data == 0)
	return ENOMEM;
    krb5_princ_set_realm_length(princ, rlen);
    tmpdata = malloc(rlen);
    if (!tmpdata) {
	free (data);
	return ENOMEM;
    }
    krb5_princ_set_realm_data(princ, tmpdata);
    memcpy(tmpdata, realm, rlen);

    /* process rest of components */

    for (i = 0, next = va_arg(ap, char *);
	 next;
	 next = va_arg(ap, char *), i++) {
	if (i == count) {
	    /* not big enough.  realloc the array */
	    krb5_data *p_tmp;
	    p_tmp = (krb5_data *) realloc((char *)data,
					  sizeof(krb5_data)*(count*2));
	    if (!p_tmp) {
	    free_out:
		    while (i-- >= 0)
			xfree(data[i].data);
		    xfree(data);
		    xfree(tmpdata);
		    return (ENOMEM);
	    }
	    count *= 2;
	    data = p_tmp;
	}

	data[i].length = strlen(next);
	data[i].data = strdup(next);
	if (!data[i].data)
	    goto free_out;
    }
    princ->data = data;
    princ->length = i;
    /* Set princ->type */
    return 0;
}

krb5_error_code
#if __STDC__ || defined(STDARG_PROTOTYPES)
krb5_build_principal(krb5_principal *princ, int rlen, const char *realm, ...)
#else
krb5_build_principal(princ, rlen, realm, va_alist)
krb5_principal *princ;
int rlen;
const char *realm;
va_dcl
#endif
{
    va_list ap;
    krb5_error_code retval;
    krb5_principal pr_ret = (krb5_principal)malloc(sizeof(krb5_principal_data));

    if (!pr_ret)
	return ENOMEM;

#if __STDC__ || defined(STDARG_PROTOTYPES)
    va_start(ap, realm);
#else
    va_start(ap);
#endif
    retval = krb5_build_principal_va(pr_ret, rlen, realm, ap);
    va_end(ap);
    if (retval == 0)
	*princ = pr_ret;
    return retval;
}
