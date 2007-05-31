/*
 * $Header$
 *
 * Copyright 2006 Massachusetts Institute of Technology.
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

#include "ccs_common.h"
#include "ccs_os_server.h"

/* Server Globals: */

cci_uuid_string_t g_server_id = NULL;
ccs_cache_collection_t g_cache_collection = NULL;
ccs_pipe_array_t g_client_pipe_array = NULL;

/* ------------------------------------------------------------------------ */

 cc_int32 ccs_server_initialize (void)
{
    cc_int32 err = ccNoError;

    if (!err) {
        err = cci_identifier_new_uuid (&g_server_id);
    }
    
    if (!err) {
        err = ccs_cache_collection_new (&g_cache_collection);
    }
    
    if (!err) {
        err = ccs_pipe_array_new (&g_client_pipe_array);
    }
    
    return cci_check_error (err);    
}

/* ------------------------------------------------------------------------ */

 cc_int32 ccs_server_cleanup (void)
{
    cc_int32 err = ccNoError;
    
    if (!err) {
        free (g_server_id);
        cci_check_error (ccs_cache_collection_release (g_cache_collection));
        cci_check_error (ccs_pipe_array_release (g_client_pipe_array));
    }
    
    return cci_check_error (err);    
}

#pragma mark -

/* ------------------------------------------------------------------------ */

cc_int32 ccs_server_new_identifier (cci_identifier_t *out_identifier)
{
    return cci_check_error (cci_identifier_new (out_identifier,
                                                g_server_id));
}

#pragma mark -

/* ------------------------------------------------------------------------ */

cc_int32 ccs_server_add_client (ccs_os_pipe_t in_connection_os_pipe)
{
    cc_int32 err = ccNoError;
    ccs_pipe_t connection_pipe = NULL;
    
    if (!err) {
        err = ccs_pipe_new (&connection_pipe, in_connection_os_pipe);
    }
    
    if (!err) {
        cci_debug_printf ("%s: Adding client %p.", __FUNCTION__, connection_pipe);
        err = ccs_pipe_array_insert (g_client_pipe_array, 
                                     connection_pipe,
                                     ccs_pipe_array_count (g_client_pipe_array));
    }

    return cci_check_error (err);    
}

/* ------------------------------------------------------------------------ */

cc_int32 ccs_server_remove_client (ccs_os_pipe_t in_connection_os_pipe)
{
    cc_int32 err = ccNoError;

    if (!err) {
        cc_uint64 i;
        cc_uint64 count = ccs_pipe_array_count (g_client_pipe_array);
        cc_uint32 found = 0;
        
        for (i = 0; !err && i < count; i++) {
            ccs_pipe_t client = ccs_pipe_array_object_at_index (g_client_pipe_array, i);
            
            err = ccs_pipe_compare_to_os_pipe (client, in_connection_os_pipe, &found);
            
            if (!err && found) {
                cci_debug_printf ("%s: Removing client %p.", __FUNCTION__, client);
                err = ccs_pipe_array_remove (g_client_pipe_array, i);
                break;
            }
        }
        
        if (!err && !found) {
            cci_debug_printf ("WARNING %s() didn't find client in client list.", 
                              __FUNCTION__);
        }        
    }
    
    return cci_check_error (err);    
}

#pragma mark -

/* ------------------------------------------------------------------------ */

static cc_int32 ccs_server_request_demux (ccs_pipe_t              in_client_pipe,
                                          ccs_pipe_t              in_reply_pipe,
                                          ccs_cache_collection_t  in_cache_collection,
                                          enum cci_msg_id_t       in_request_name,
                                          cci_identifier_t        in_request_identifier,
                                          cci_stream_t            in_request_data,
                                          cc_uint32              *out_reply_immediately,
                                          cci_stream_t           *out_reply_data)
{
    cc_int32 err = ccNoError;

    if (!ccs_pipe_valid (in_reply_pipe)) { err = cci_check_error (ccErrBadParam); }
    if (!in_request_data               ) { err = cci_check_error (ccErrBadParam); }
    if (!out_reply_immediately         ) { err = cci_check_error (ccErrBadParam); }
    if (!out_reply_data                ) { err = cci_check_error (ccErrBadParam); }

    if (!err) {
        if (in_request_name > cci_context_first_msg_id &&
            in_request_name < cci_context_last_msg_id) {
            /* Note: context identifier doesn't need to match.  
             * Client just uses the identifier to detect server relaunch. */
            
            if (!err) {
                err = ccs_cache_collection_handle_message (in_client_pipe,
                                                           in_reply_pipe,
                                                           in_cache_collection,
                                                           in_request_name,
                                                           in_request_data,
                                                           out_reply_immediately,
                                                           out_reply_data);
            }
                
        } else if (in_request_name > cci_ccache_first_msg_id &&
                   in_request_name < cci_ccache_last_msg_id) {
            ccs_ccache_t ccache = NULL;
            
            err = ccs_cache_collection_find_ccache (in_cache_collection,
                                                    in_request_identifier,
                                                    &ccache);            
            
            if (!err) {
                err = ccs_ccache_handle_message (in_client_pipe,
                                                 in_reply_pipe,
                                                 ccache,
                                                 in_cache_collection,
                                                 in_request_name,
                                                 in_request_data,
                                                 out_reply_immediately,
                                                 out_reply_data);
            }                
                        
        } else if (in_request_name > cci_ccache_iterator_first_msg_id &&
                   in_request_name < cci_ccache_iterator_last_msg_id) {
            ccs_ccache_iterator_t ccache_iterator = NULL;
            
            err = ccs_cache_collection_find_ccache_iterator (in_cache_collection,
                                                             in_request_identifier,
                                                             &ccache_iterator);            
            
            if (!err) {
                err = ccs_ccache_iterator_handle_message (ccache_iterator,
                                                          in_cache_collection,
                                                          in_request_name,
                                                          in_request_data,
                                                          out_reply_data);
            }
            
            if (!err) {
                *out_reply_immediately = 1; /* can't block */
            }
            
        } else if (in_request_name > cci_credentials_iterator_first_msg_id &&
                   in_request_name < cci_credentials_iterator_last_msg_id) {
            ccs_credentials_iterator_t credentials_iterator = NULL;
            ccs_ccache_t ccache = NULL;
            
            err = ccs_cache_collection_find_credentials_iterator (in_cache_collection,
                                                                  in_request_identifier,
                                                                  &ccache,
                                                                  &credentials_iterator);            
            
            if (!err) {
                err = ccs_credentials_iterator_handle_message (credentials_iterator,
                                                               ccache,
                                                               in_request_name,
                                                               in_request_data,
                                                               out_reply_data);
            }
            
            if (!err) {
                *out_reply_immediately = 1; /* can't block */
            }
 
        } else {
            err = ccErrBadInternalMessage;
        }
    }
    
    return cci_check_error (err);
}

#pragma mark -

/* ------------------------------------------------------------------------ */

cc_int32 ccs_server_handle_request (ccs_pipe_t     in_client_pipe,
                                    ccs_pipe_t     in_reply_pipe,
                                    cci_stream_t   in_request)
{
    cc_int32 err = ccNoError;
    enum cci_msg_id_t request_name = 0;
    cci_identifier_t request_identifier = NULL;
    cc_uint32 reply_immediately = 1;
    cci_stream_t reply_data = NULL;
    
    if (!ccs_pipe_valid (in_client_pipe)) { err = cci_check_error (ccErrBadParam); }
    if (!ccs_pipe_valid (in_reply_pipe) ) { err = cci_check_error (ccErrBadParam); }
    if (!in_request                     ) { err = cci_check_error (ccErrBadParam); }
    
    if (!err) {
        err = cci_message_read_request_header (in_request,
                                               &request_name,
                                               &request_identifier);
    }
    
    if (!err) {
        cc_uint32 server_err = 0;
        cc_uint32 valid = 0;
        ccs_cache_collection_t cache_collection = g_cache_collection;
        
        server_err = cci_identifier_is_for_server (request_identifier, 
                                                   g_server_id, 
                                                   &valid);
        
        if (!server_err && !valid) {
            server_err = cci_message_invalid_object_err (request_name);
        }
        
        if (!server_err) {
            
            /* Monolithic server implementation would need to select 
             * cache collection here.  Currently we only support per-user
             * servers so we always use the same cache collection. */
            
            server_err = ccs_server_request_demux (in_client_pipe,
                                                   in_reply_pipe,
                                                   cache_collection,
                                                   request_name,
                                                   request_identifier,
                                                   in_request, 
                                                   &reply_immediately,
                                                   &reply_data);
        }
        
        if (server_err || reply_immediately) {
            err = ccs_server_send_reply (in_reply_pipe, server_err, reply_data);
        }
    }

    cci_identifier_release (request_identifier);
    
    return cci_check_error (err);    
}

/* ------------------------------------------------------------------------ */

cc_int32 ccs_server_send_reply (ccs_pipe_t     in_reply_pipe,
                                cc_int32       in_reply_err,
                                cci_stream_t   in_reply_data)
{
    cc_int32 err = ccNoError;
    cci_stream_t reply = NULL;
    
    if (!ccs_pipe_valid (in_reply_pipe) ) { err = cci_check_error (ccErrBadParam); }
    
    if (!err) {
        err = cci_message_new_reply_header (&reply, in_reply_err);
    }
    
    if (!err && in_reply_data && cci_stream_size (in_reply_data) > 0) {
        err = cci_stream_write (in_reply_data, 
                                cci_stream_data (in_reply_data), 
                                cci_stream_size (in_reply_data));
    }
    
    if (!err) {
        err = ccs_os_server_send_reply (in_reply_pipe, reply);
    }
    
    cci_stream_release (reply);
    
    return cci_check_error (err);    
}