/*
 * $Source$
 * $Author$
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
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
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_finish_key_c[] =
"$Id$";
#endif	/* !lint & !SABER */

#include <krb5/krb5.h>
#include <krb5/ext-proto.h>

#include <krb5/mit-des.h>

/*
	does any necessary clean-up on the eblock (such as releasing
	resources held by eblock->priv).

	returns: errors
 */

krb5_error_code mit_des_finish_key (DECLARG(krb5_encrypt_block *,eblock))
OLDDECLARG(krb5_encrypt_block *,eblock)
{
    memset((char *)eblock->priv, 0, sizeof(mit_des_key_schedule));
    free(eblock->priv);
    eblock->priv = 0;
    /* free/clear other stuff here? */
    return 0;
}
