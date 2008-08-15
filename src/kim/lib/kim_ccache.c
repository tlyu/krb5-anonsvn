/*
 * Copyright 2005-2006 Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 * require a specific license from the United States Government.
 * It is the responsibility of any person or organization contemplating
 * export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */

#include <krb5.h>
#include <Kerberos/CredentialsCache.h>
#include "kim_private.h"

struct kim_ccache_iterator_opaque {
    krb5_context context;
    krb5_cccol_cursor cursor;
};

struct kim_ccache_iterator_opaque kim_ccache_iterator_initializer = { NULL, NULL };

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_iterator_create (kim_ccache_iterator *out_ccache_iterator)
{
    kim_error err = KIM_NO_ERROR;
    kim_ccache_iterator ccache_iterator = NULL;
    
    if (!err && !out_ccache_iterator) { err = param_error (1, "out_ccache_iterator", "NULL"); }
    
    if (!err) {
        ccache_iterator = malloc (sizeof (*ccache_iterator));
        if (ccache_iterator) { 
            *ccache_iterator = kim_ccache_iterator_initializer;
        } else {
            err = os_error (errno); 
        }
    }
    
    if (!err) {
        err = krb5_error (NULL, krb5_init_context (&ccache_iterator->context));
    }
    
    if (!err) {
        err = krb5_error (ccache_iterator->context,
                          krb5_cccol_cursor_new (ccache_iterator->context,
                                                 &ccache_iterator->cursor));
    }
    
    if (!err) {    
        *out_ccache_iterator = ccache_iterator;
        ccache_iterator = NULL;
    }
    
    kim_ccache_iterator_free (&ccache_iterator);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_iterator_next (kim_ccache_iterator  in_ccache_iterator,
                                    kim_ccache          *out_ccache)
{
    kim_error err = KIM_NO_ERROR;
    krb5_ccache ccache = NULL;
    
    if (!err && !in_ccache_iterator) { err = param_error (1, "in_ccache_iterator", "NULL"); }
    if (!err && !out_ccache        ) { err = param_error (2, "out_ccache", "NULL"); }
    
    if (!err) {
        krb5_error_code terr = krb5_cccol_cursor_next (in_ccache_iterator->context, 
                                                       in_ccache_iterator->cursor,
                                                       &ccache);
        
        if (!terr) {
            err = kim_ccache_create_from_krb5_ccache (out_ccache,
                                                      in_ccache_iterator->context, 
                                                      ccache);
        } else if (terr == KRB5_CC_END) {
            *out_ccache = NULL; /* no more ccaches */
            
        } else {
            err = krb5_error (in_ccache_iterator->context, terr);
        }
    }
    
    if (ccache) { krb5_cc_close (in_ccache_iterator->context, ccache); }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

void kim_ccache_iterator_free (kim_ccache_iterator *io_ccache_iterator)
{
    if (io_ccache_iterator && *io_ccache_iterator) {
        if ((*io_ccache_iterator)->context) { 
            if ((*io_ccache_iterator)->cursor) {
                krb5_cccol_cursor_free ((*io_ccache_iterator)->context, 
                                        &(*io_ccache_iterator)->cursor);
            }
            krb5_free_context ((*io_ccache_iterator)->context); 
        }
        free (*io_ccache_iterator);
        *io_ccache_iterator = NULL;
    }
}

#pragma mark -

/* ------------------------------------------------------------------------ */

struct kim_ccache_opaque {
    krb5_context context;
    krb5_ccache ccache;
};

struct kim_ccache_opaque kim_ccache_initializer = { NULL, NULL };

/* ------------------------------------------------------------------------ */

static kim_error kim_ccache_create_resolve_name (kim_string *out_resolve_name,
                                                 kim_string  in_name,
                                                 kim_string  in_type)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !out_resolve_name) { err = param_error (1, "out_resolve_name", "NULL"); }
    if (!err && !in_name         ) { err = param_error (2, "in_name", "NULL"); }
    if (!err && !in_type         ) { err = param_error (2, "in_type", "NULL"); }
    
    if (!err) {
        err = kim_string_create_from_format (out_resolve_name, "%s:%s", 
                                             in_type, in_name);
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

static kim_boolean kim_ccache_k5ccaches_are_equal (krb5_context in_context,
                                                   krb5_ccache  in_ccache,
                                                   krb5_context in_compare_to_context,
                                                   krb5_ccache  io_compare_to_ccache)
{
    kim_boolean equal = FALSE;
    
    if (in_context && in_ccache && in_compare_to_context && io_compare_to_ccache) {
        const char *type            = krb5_cc_get_type (in_context, in_ccache);
        const char *compare_to_type = krb5_cc_get_type (in_compare_to_context, 
                                                        io_compare_to_ccache);
        const char *name            = krb5_cc_get_name (in_context, in_ccache);
        const char *compare_to_name = krb5_cc_get_name (in_compare_to_context, 
                                                        io_compare_to_ccache);
        
        equal = (!strcmp (type, compare_to_type) && 
                 !strcmp (name, compare_to_name));
    }
    
    return equal;
}

#pragma mark -

/* ------------------------------------------------------------------------ */

static inline kim_error kim_ccache_allocate (kim_ccache *out_ccache)
{
    kim_error err = KIM_NO_ERROR;
    kim_ccache ccache = NULL;
    
    if (!err && !out_ccache) { err = param_error (1, "out_ccache", "NULL"); }
    
    if (!err) {
        ccache = malloc (sizeof (*ccache));
        if (!ccache) { err = os_error (errno); }
    }
    
    if (!err) {
        *ccache = kim_ccache_initializer;
        *out_ccache = ccache;
        ccache = NULL;
    }
    
    kim_ccache_free (&ccache);
    
    return check_error (err);    
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_new (kim_ccache          *out_ccache,
                                 kim_identity         in_client_identity,
                                 kim_options          in_options)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    kim_identity client_identity = NULL;
    
    if (!err && !out_ccache) { err = param_error (1, "out_ccache", "NULL"); }
    
    if (!err) {
        err = kim_credential_create_new (&credential, in_client_identity, in_options);
    }
    
    if (!err) {
        err = kim_credential_get_client_identity (credential, &client_identity);
    }
    
    if (!err) {
        err = kim_credential_store (credential, client_identity, out_ccache);
    }
    
    kim_identity_free (&client_identity);
    kim_credential_free (&credential);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_new_if_needed (kim_ccache   *out_ccache,
                                           kim_identity  in_client_identity,
                                           kim_options   in_options)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !out_ccache        ) { err = param_error (1, "out_ccache", "NULL"); }
    if (!err && !in_client_identity) { err = param_error (2, "in_client_identity", "NULL"); }
    
    if (!err) {
        err = kim_ccache_create_from_client_identity (out_ccache, in_client_identity);
        
        if (err) {
            kim_error_free (&err);  /* toss error since we don't care */
            err = kim_ccache_create_new (out_ccache, in_client_identity, in_options);
        }
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_from_client_identity (kim_ccache   *out_ccache,
                                                  kim_identity  in_client_identity)
{
    kim_error err = KIM_NO_ERROR;
    kim_ccache_iterator iterator = NULL;
    kim_boolean found = FALSE;
    
    if (!err && !out_ccache        ) { err = param_error (1, "out_ccache", "NULL"); }
    if (!err && !in_client_identity) { err = param_error (2, "in_client_identity", "NULL"); }
    
    if (!err) {
        err = kim_ccache_iterator_create (&iterator);
    }
    
    while (!err && !found) {
        kim_ccache ccache = NULL;
        kim_identity identity = NULL;
        
        err = kim_ccache_iterator_next (iterator, &ccache);
        
        if (!err && !ccache) {
            kim_string string = NULL;
            
            err = kim_identity_get_display_string (in_client_identity, &string);
            
            if (!err) {
                err = kim_error_create_from_code (KIM_NO_SUCH_PRINCIPAL_ECODE, 
                                                  string);
            }
            
            kim_string_free (&string);
        }
        
        if (!err) {
            err = kim_ccache_get_client_identity (ccache, &identity);
        }
        
        if (!err) {
            err = kim_identity_compare (in_client_identity, identity, &found);
        }
        
        if (!err && found) {
            *out_ccache = ccache;
            ccache = NULL;
        }
        
        kim_identity_free (&identity);
        kim_ccache_free (&ccache);
    }
    
    kim_ccache_iterator_free (&iterator);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_from_keytab (kim_ccache    *out_ccache,
                                         kim_identity   in_identity,
                                         kim_options    in_options,
                                         kim_string     in_keytab)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    kim_identity client_identity = NULL;
    
    if (!err && !out_ccache) { err = param_error (1, "out_ccache", "NULL"); }
    
    if (!err) {
        err = kim_credential_create_from_keytab (&credential, in_identity, 
                                                 in_options, in_keytab);
    }
    
    if (!err) {
        err = kim_credential_get_client_identity (credential, &client_identity);
    }
    
    if (!err) {
        err = kim_credential_store (credential, client_identity, out_ccache);
    }
    
    kim_identity_free (&client_identity);
    kim_credential_free (&credential);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_from_default (kim_ccache *out_ccache)
{
    kim_error err = KIM_NO_ERROR;
    kim_ccache ccache = NULL;
    
    if (!err && !out_ccache) { err = param_error (1, "out_ccache", "NULL"); }
    
    if (!err) {
        err = kim_ccache_allocate (&ccache);
    }
    
    if (!err) {
        err = krb5_error (NULL, krb5_init_context (&ccache->context));
    }
    
    if (!err) {
        err = krb5_error (ccache->context,
                          krb5_cc_default (ccache->context, &ccache->ccache));
    }
    
    if (!err) {
        *out_ccache = ccache;
        ccache = NULL;
    }
    
    kim_ccache_free (&ccache);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_from_display_name (kim_ccache  *out_ccache,
                                               kim_string   in_display_name)
{
    kim_error err = KIM_NO_ERROR;
    kim_ccache ccache = NULL;
    
    if (!err && !out_ccache     ) { err = param_error (1, "out_ccache", "NULL"); }
    if (!err && !in_display_name) { err = param_error (2, "in_display_name", "NULL"); }
    
    if (!err) {
        err = kim_ccache_allocate (&ccache);
    }
    
    if (!err) {
        err = krb5_error (NULL, krb5_init_context (&ccache->context));
    }
    
    if (!err) {
        err = krb5_error (ccache->context,
                          krb5_cc_resolve (ccache->context, in_display_name, 
                                           &ccache->ccache));
    }
    
    if (!err) {
        *out_ccache = ccache;
        ccache = NULL;
    }
    
    kim_ccache_free (&ccache);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_from_type_and_name (kim_ccache  *out_ccache,
                                                kim_string   in_type,
                                                kim_string   in_name)
{
    kim_error err = KIM_NO_ERROR;
    kim_string resolve_name = NULL;
    
    if (!err && !out_ccache) { err = param_error (1, "out_ccache", "NULL"); }
    if (!err && !in_name   ) { err = param_error (2, "in_name", "NULL"); }
    if (!err && !in_type   ) { err = param_error (2, "in_type", "NULL"); }
    
    if (!err) {
        err = kim_ccache_create_resolve_name (&resolve_name, in_name, in_type);
    }
    
    if (!err) {
        err = kim_ccache_create_from_display_name (out_ccache, resolve_name);
    }
    
    kim_string_free (&resolve_name);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_create_from_krb5_ccache (kim_ccache  *out_ccache,
                                              krb5_context   in_krb5_context,
                                              krb5_ccache    in_krb5_ccache)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !out_ccache     ) { err = param_error (1, "out_ccache", "NULL"); }
    if (!err && !in_krb5_ccache ) { err = param_error (2, "in_krb5_ccache", "NULL"); }
    if (!err && !in_krb5_context) { err = param_error (3, "in_krb5_context", "NULL"); }
    
    if (!err) {
        kim_string type = krb5_cc_get_type (in_krb5_context, in_krb5_ccache);
        kim_string name = krb5_cc_get_name (in_krb5_context, in_krb5_ccache);
        
        err = kim_ccache_create_from_type_and_name (out_ccache, type, name);
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_copy (kim_ccache  *out_ccache,
                           kim_ccache   in_ccache)
{
    kim_error err = KIM_NO_ERROR;
    kim_string name = NULL;
    kim_string type = NULL;
    
    if (!err && !out_ccache) { err = param_error (1, "out_ccache", "NULL"); }
    if (!err && !in_ccache ) { err = param_error (2, "in_ccache", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_name (in_ccache, &name);
    }
    
    if (!err) {
        err = kim_ccache_get_type (in_ccache, &type);
    }
    
    if (!err) {
        err = kim_ccache_create_from_type_and_name (out_ccache, type, name);
    }
    
    kim_string_free (&name);
    kim_string_free (&type);
    
    return check_error (err);
}

#pragma mark -

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_compare (kim_ccache   in_ccache,
                              kim_ccache   in_compare_to_ccache,
                              kim_boolean *out_equal)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !in_ccache           ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !in_compare_to_ccache) { err = param_error (2, "in_compare_to_ccache", "NULL"); }
    if (!err && !out_equal           ) { err = param_error (3, "out_equal", "NULL"); }
    
    if (!err) {
        *out_equal = kim_ccache_k5ccaches_are_equal (in_ccache->context, 
                                                     in_ccache->ccache,
                                                     in_compare_to_ccache->context, 
                                                     in_compare_to_ccache->ccache);
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_krb5_ccache (kim_ccache  in_ccache,
                                      krb5_context  in_krb5_context,
                                      krb5_ccache  *out_krb5_ccache)
{
    kim_error err = KIM_NO_ERROR;
    kim_string resolve_name = NULL;
    
    if (!err && !in_ccache      ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !in_krb5_context) { err = param_error (2, "in_krb5_context", "NULL"); }
    if (!err && !out_krb5_ccache) { err = param_error (2, "out_krb5_ccache", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_display_name (in_ccache, &resolve_name);
    }
    
    if (!err) {
        err = krb5_error (in_krb5_context,
                          krb5_cc_resolve (in_krb5_context, resolve_name, 
                                           out_krb5_ccache));
    }
    
    kim_string_free (&resolve_name);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_type (kim_ccache  in_ccache,
                               kim_string *out_type)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !in_ccache) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_type ) { err = param_error (2, "out_type", "NULL"); }
    
    if (!err) {
        err = kim_string_copy (out_type, krb5_cc_get_type (in_ccache->context, 
                                                           in_ccache->ccache));
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_name (kim_ccache  in_ccache,
                               kim_string *out_name)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !in_ccache) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_name ) { err = param_error (2, "out_name", "NULL"); }
    
    if (!err) {
        err = kim_string_copy (out_name, krb5_cc_get_name (in_ccache->context, 
                                                           in_ccache->ccache));
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_display_name (kim_ccache  in_ccache,
                                       kim_string *out_display_name)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !in_ccache       ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_display_name) { err = param_error (2, "out_display_name", "NULL"); }
    
    if (!err) {
        kim_string type = krb5_cc_get_type (in_ccache->context, 
                                            in_ccache->ccache);
        kim_string name = krb5_cc_get_name (in_ccache->context, 
                                            in_ccache->ccache);
        
        err = kim_ccache_create_resolve_name (out_display_name, type, name);
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_client_identity (kim_ccache    in_ccache,
                                          kim_identity *out_client_identity)
{
    kim_error err = KIM_NO_ERROR;
    krb5_principal principal = NULL;
    
    if (!err && !in_ccache          ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_client_identity) { err = param_error (2, "out_client_identity", "NULL"); }
    
    if (!err) {
        err = krb5_error (in_ccache->context,
                          krb5_cc_get_principal (in_ccache->context, 
                                                 in_ccache->ccache, 
                                                 &principal));
    }
    
    if (!err) {
        err = kim_identity_create_from_krb5_principal (out_client_identity,
                                                       in_ccache->context, 
                                                       principal);
    }
    
    if (principal) { krb5_free_principal (in_ccache->context, principal); }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

static kim_error kim_ccache_get_dominant_credential (kim_ccache            in_ccache,
                                                     kim_credential_state *out_state,
                                                     kim_boolean          *out_is_tgt,
                                                     kim_credential       *out_credential)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential_iterator iterator = NULL;
    kim_boolean out_of_credentials = FALSE;
    kim_boolean found_valid_tgt = FALSE;
    kim_boolean dominant_is_tgt = FALSE;
    kim_credential_state dominant_state = kim_credentials_state_valid;
    kim_credential dominant_credential = NULL;
    
    if (!err && !in_ccache) { err = param_error (1, "in_ccache", "NULL"); }
    
    if (!err) {
        err = kim_credential_iterator_create (&iterator, in_ccache);
    }
    
    while (!err && !out_of_credentials && !found_valid_tgt) {
        kim_credential credential = NULL;
        
        err = kim_credential_iterator_next (iterator, &credential);
        
        if (!err && !credential) {
            out_of_credentials = TRUE;
            
        } else if (!err) {
            kim_credential_state state = kim_credentials_state_valid;
            kim_boolean is_tgt = FALSE;
            
            err = kim_credential_get_state (credential, &state);
            
            if (!err) {
                kim_identity service_identity = NULL;
                
                err = kim_credential_get_service_identity (credential, 
                                                           &service_identity);
                
                if (!err) {
                    err = kim_identity_is_tgt_service (service_identity, &is_tgt);
                }
                
                kim_identity_free (&service_identity);
            }
            
            if (!err) {
                /* There are three cases where we replace: 
                 * 1) We don't have a dominant yet
                 * 2) This is a tgt and dominant isn't
                 * 3) Both are tgts but this is valid and dominant isn't */
                
                if ((!dominant_credential)        /* 1 */ || 
                    (is_tgt && !dominant_is_tgt)  /* 2 */ ||
                    (is_tgt && dominant_is_tgt && /* 3 */ 
                     state == kim_credentials_state_valid &&
                     dominant_state != kim_credentials_state_valid)) {
                    /* replace */
                    kim_credential_free (&dominant_credential);
                    
                    dominant_credential = credential;
                    credential = NULL; /* take ownership */
                    
                    dominant_is_tgt = is_tgt;
                    dominant_state = state;
                }
                
                if (dominant_is_tgt && 
                    dominant_state == kim_credentials_state_valid) {
                    /* Since we will never replace a valid tgt, stop here */
                    found_valid_tgt = TRUE;
                }
            }
        }
        
        kim_credential_free (&credential);
    }
    
    if (!err && !dominant_credential) {
        kim_identity identity = NULL;
        kim_string identity_string = NULL;
        
        err = kim_ccache_get_client_identity (in_ccache, &identity);
        
        if (!err) {
            err = kim_identity_get_display_string (identity, 
                                                   &identity_string);
        }
        
        if (!err) {
            err = kim_error_create_from_code (KIM_NO_CREDENTIALS_ECODE, 
                                              identity_string);
        }    

        kim_string_free (&identity_string);
        kim_identity_free (&identity);
    }
        
    if (!err) {
        if (out_is_tgt) {
            *out_is_tgt = dominant_is_tgt;
        }
        
        if (out_state) {
            *out_state = dominant_state;
        }
         
        if (out_credential) {
            *out_credential = dominant_credential;
            dominant_credential = NULL;  /* take ownership */
        }
    }
    
    kim_credential_free (&dominant_credential);
    kim_credential_iterator_free (&iterator);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_valid_credential (kim_ccache      in_ccache,
                                           kim_credential *out_credential)
{
    kim_error err = KIM_NO_ERROR;
    kim_boolean is_tgt = FALSE;
    kim_credential_state state = kim_credentials_state_valid;
    kim_credential credential = NULL;
    
    if (!err && !in_ccache     ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_credential) { err = param_error (2, "out_credential", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_dominant_credential (in_ccache, 
                                                  &state, &is_tgt, &credential);
    }
    
    if (!err && state != kim_credentials_state_valid) {
        kim_identity identity = NULL;
        kim_string identity_string = NULL;
        
        err = kim_ccache_get_client_identity (in_ccache, &identity);
        
        if (!err) {
            err = kim_identity_get_display_string (identity, 
                                                   &identity_string);
        }
        
        if (!err) {
            if (state == kim_credentials_state_expired) {
                err = kim_error_create_from_code (KIM_CREDENTIALS_EXPIRED_ECODE, 
                                                  identity_string);
                
            } else if (state == kim_credentials_state_not_yet_valid ||
                       state == kim_credentials_state_needs_validation) {
                err = kim_error_create_from_code (KIM_NEEDS_VALIDATION_ECODE, 
                                                  identity_string);
                
            } else if (state == kim_credentials_state_address_mismatch) {
                err = kim_error_create_from_code (KIM_BAD_IP_ADDRESS_ECODE, 
                                                  identity_string);                
            } else {
                /* just default to this */
                err = kim_error_create_from_code (KIM_NEEDS_VALIDATION_ECODE, 
                                                  identity_string);
            }
        }
        
        kim_string_free (&identity_string);
        kim_identity_free (&identity);
    }
    
    if (!err) {
        *out_credential = credential;
        credential = NULL;  /* take ownership */
    }
    
    kim_credential_free (&credential);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_state (kim_ccache            in_ccache,
                                kim_credential_state *out_state)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !in_ccache) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_state) { err = param_error (2, "out_state", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_dominant_credential (in_ccache, 
                                                  out_state, NULL, NULL);
    }
    
    return check_error (err);    
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_start_time (kim_ccache  in_ccache,
                                     kim_time   *out_start_time)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    
    if (!err && !in_ccache     ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_start_time) { err = param_error (2, "out_start_time", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_dominant_credential (in_ccache, NULL, NULL, 
                                                  &credential);
    }
    
    if (!err) {
        err = kim_credential_get_start_time (credential, out_start_time);
    }
    
    kim_credential_free (&credential);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_expiration_time (kim_ccache  in_ccache,
                                          kim_time   *out_expiration_time)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    
    if (!err && !in_ccache          ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_expiration_time) { err = param_error (2, "out_expiration_time", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_dominant_credential (in_ccache, NULL, NULL, 
                                                  &credential);
    }
    
    if (!err) {
        err = kim_credential_get_expiration_time (credential, 
                                                  out_expiration_time);
    }
    
    kim_credential_free (&credential);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_get_renewal_expiration_time (kim_ccache  in_ccache,
                                                  kim_time   *out_renewal_expiration_time)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    
    if (!err && !in_ccache                  ) { err = param_error (1, "in_ccache", "NULL"); }
    if (!err && !out_renewal_expiration_time) { err = param_error (2, "out_renewal_expiration_time", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_dominant_credential (in_ccache, NULL, NULL, 
                                                  &credential);
    }
    
    if (!err) {
        err = kim_credential_get_renewal_expiration_time (credential, 
                                                          out_renewal_expiration_time);
    }
    
    kim_credential_free (&credential);
    
    return check_error (err);
}

#pragma mark -

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_set_default (kim_ccache io_ccache)
{
    kim_error err = KIM_NO_ERROR;
    
    if (!err && !io_ccache) { err = param_error (1, "io_ccache", "NULL"); }
    
    if (!err) {
        char *environment_ccache_name = getenv ("KRB5CCNAME");
        
        if (environment_ccache_name) {
            krb5_ccache environment_ccache = NULL;
            krb5_principal client_principal = NULL;
            
            err = krb5_error (io_ccache->context,
                              krb5_cc_resolve (io_ccache->context, 
                                               environment_ccache_name, 
                                               &environment_ccache));
            
            if (!err && !kim_ccache_k5ccaches_are_equal (io_ccache->context, 
                                                         io_ccache->ccache,
                                                         io_ccache->context, 
                                                         environment_ccache)) {
                /* KRB5CCNAME is set and does not point to this ccache.  
                 * Move the creds and make this kim_ccache_t object refer to that ccache.  */
                
                err = krb5_error (io_ccache->context,
                                  krb5_cc_get_principal (io_ccache->context, 
                                                         io_ccache->ccache, 
                                                         &client_principal));
                
                if (!err) {
                    err = krb5_error (io_ccache->context,
                                      krb5_cc_initialize (io_ccache->context, 
                                                          environment_ccache, 
                                                          client_principal));
                }
                
                if (!err) {
                    err = krb5_error (io_ccache->context,
                                      krb5_cc_copy_creds (io_ccache->context, 
                                                          io_ccache->ccache, 
                                                          environment_ccache));
                }
                
                if (!err) {
                    krb5_cc_destroy (io_ccache->context, io_ccache->ccache);
                    io_ccache->ccache = environment_ccache;
                    environment_ccache = NULL; /* take ownership */
                }
            }
            
            if (environment_ccache) { krb5_cc_close (io_ccache->context, 
                                                     environment_ccache); }
            
        } else {
#ifdef USE_CCAPI
            kim_string type = NULL;
            kim_string name = NULL;
            cc_context_t cc_context = NULL;
            cc_ccache_t cc_ccache = NULL;
            
            err = kim_ccache_get_type (io_ccache, &type);
            
            if (!err && strcmp (type, "API")) {
#endif
                kim_string display_name = NULL;
                /* Not a CCAPI ccache; can't set to default */
                
                err = kim_ccache_get_display_name (io_ccache, &display_name);
                
                if (!err) {
                    err = kim_error_create_from_code (KIM_CANT_BECOME_DEFAULT_ECODE, 
                                                      display_name);
                }
                
                kim_string_free (&display_name);
#ifdef USE_CCAPI
            }
            
            if (!err) {
                err = kim_ccache_get_name (io_ccache, &name);
            }
            
            /* get a CCAPI ccache for this cache */
            if (!err) {
                err = ccapi_error (cc_initialize (&cc_context, ccapi_version_4, 
                                                  NULL, NULL));
            }
            
            if (!err) {
                err = ccapi_error (cc_context_open_ccache (cc_context, name,
                                                           &cc_ccache));
            }
            
            if (!err) {
                err = ccapi_error (cc_ccache_set_default (cc_ccache));
            }
            
            if (cc_context) { cc_context_release (cc_context); }
            if (cc_ccache ) { cc_ccache_release (cc_ccache); }
            kim_string_free (&name);
            kim_string_free (&type);
#endif
        }
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_verify (kim_ccache   in_ccache,
                             kim_identity in_service_identity,
                             kim_string   in_keytab,
                             kim_boolean  in_fail_if_no_service_key)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    
    if (!err && !in_ccache) { err = param_error (1, "in_ccache", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_valid_credential (in_ccache, &credential);
    }
    
    if (!err) {
        err = kim_credential_verify (credential, in_service_identity, 
                                     in_keytab, in_fail_if_no_service_key);
    }
    
    kim_credential_free (&credential);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_renew (kim_ccache  in_ccache,
                            kim_options in_options)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    kim_identity client_identity = NULL;
    
    if (!err && !in_ccache) { err = param_error (1, "in_ccache", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_valid_credential (in_ccache, &credential);
    }
    
    if (!err) {
        err = kim_credential_renew (&credential, in_options);
    }
    
    if (!err) {
        err = kim_ccache_get_client_identity (in_ccache, &client_identity);
    }
    
    if (!err) {
        err = kim_credential_store (credential, client_identity, NULL);
    }
    
    kim_identity_free (&client_identity);
    kim_credential_free (&credential);
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_validate (kim_ccache  in_ccache,
                               kim_options in_options)
{
    kim_error err = KIM_NO_ERROR;
    kim_credential credential = NULL;
    kim_identity client_identity = NULL;
    
    if (!err && !in_ccache) { err = param_error (1, "in_ccache", "NULL"); }
    
    if (!err) {
        err = kim_ccache_get_valid_credential (in_ccache, &credential);
    }
    
    if (!err) {
        err = kim_credential_validate (&credential, in_options);
    }
    
    if (!err) {
        err = kim_ccache_get_client_identity (in_ccache, &client_identity);
    }
    
    if (!err) {
        err = kim_credential_store (credential, client_identity, NULL);
    }
    
    kim_identity_free (&client_identity);
    kim_credential_free (&credential);
    
    return check_error (err);
}

#pragma mark -

/* ------------------------------------------------------------------------ */

kim_error kim_ccache_destroy (kim_ccache *io_ccache)
{
    kim_error err = KIM_NO_ERROR;
    
    if (io_ccache && *io_ccache) {
        err = krb5_error ((*io_ccache)->context,
                          krb5_cc_destroy ((*io_ccache)->context, 
                                           (*io_ccache)->ccache));
        
        if (!err) { 
            (*io_ccache)->ccache = NULL; 
            kim_ccache_free (io_ccache);
        }
    }
    
    return check_error (err);
}

/* ------------------------------------------------------------------------ */

void kim_ccache_free (kim_ccache *io_ccache)
{
    if (io_ccache && *io_ccache) {
        if ((*io_ccache)->context) { 
            if ((*io_ccache)->ccache) {
                krb5_cc_close ((*io_ccache)->context, (*io_ccache)->ccache);
            }
            krb5_free_context ((*io_ccache)->context); 
        }
        free (*io_ccache);
        *io_ccache = NULL;
    }
}
