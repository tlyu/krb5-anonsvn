/*
 * $Source$
 * $Author$
 * $Id$
 *
 * Copyright 1989 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <krb5/mit-copyright.h>.
 *
 * General definitions for Kerberos version 5.
 */

#include <krb5/copyright.h>

#ifndef __KRB5_GENERAL__
#define __KRB5_GENERAL__

#include <sys/types.h>

#include <krb5/base-defs.h>
#include <krb5/hostaddr.h>
#include <krb5/encryption.h>
#include <krb5/fieldbits.h>
#include <krb5/errors.h>
#include <krb5/proto.h>
#include <krb5/tcache.h>

typedef	krb5_string *	krb5_principal;	/* array of strings */
					/* CONVENTION: realm is first elem. */

/* Time set */
typedef struct _krb5_ticket_times {
    krb5_timestamp authtime; /* XXX ? should ktime in KDC_REP == authtime
				in ticket? otherwise client can't get this */ 
    krb5_timestamp starttime;
    krb5_timestamp endtime;
    krb5_timestamp renew_till;
} krb5_ticket_times;

/* the unencrypted version */
typedef struct _krb5_ticket {
    /* cleartext portion */
    krb5_principal server;		/* server name/realm */
    krb5_enctype etype;			/* ticket encryption type */
    krb5_kvno skvno;			/* server kvno */
    /* to-be-encrypted portion */
    krb5_confounder confounder;		/* confounder */
    krb5_flags flags;			/* flags */
    krb5_keyblock *session;		/* session key: includes keytype */
    krb5_principal client;		/* client name/realm */
    krb5_string transited;		/* list of transited realms */
    krb5_ticket_times times;		/* auth, start, end, renew_till */
    krb5_address **caddrs;		/* array of ptrs to addresses */
    krb5_string authorization_data;	/* auth data */
} krb5_ticket;

/* the unencrypted version */
typedef struct _krb5_authenticator {
    krb5_principal client;		/* client name/realm */
    krb5_checksum *checksum;		/* checksum, includes type */
    krb5_ui_2 cmsec;			/* client msec portion */
    krb5_timestamp ctime;		/* client sec portion */
} krb5_authenticator;

typedef struct _krb5_tkt_authent {
    krb5_ticket ticket;
    krb5_authenticator authenticator;
} krb5_tkt_authent;

/* credentials:  Ticket, session key, etc. */
typedef struct _krb5_credentials {
    krb5_principal client;		/* client's principal identifier */
    krb5_principal server;		/* server's principal identifier */
    krb5_encrypt_block keyblock;	/* session encryption key info */
    krb5_ticket_times times;		/* lifetime info */
    krb5_flags ticket_flags;		/* flags in ticket */
    krb5_string ticket;			/* ticket string itself */
} krb5_credentials;

/* Last request fields */
typedef struct _krb5_last_req_entry {
    krb5_ui_4 value;
    octet lr_type;
} krb5_last_req_entry;

typedef struct _krb5_kdc_rep {
    /* cleartext part: */
    krb5_principal client;		/* client's principal identifier */
    krb5_enctype etype;			/* encryption type */
    krb5_kvno ckvno;			/* client key version */
    krb5_string ticket;			/* ticket (already encrypted) */
    /* encrypted part: */
    krb5_confounder confounder;		/* confounder */
    krb5_keyblock *session;		/* session key */
    krb5_last_req_entry *last_req;	/* array of ptrs to entries */
    krb5_ticket_times times;		/* lifetime info */
    krb5_timestamp key_exp;		/* expiration date */
    krb5_flags flags;			/* ticket flags */
    krb5_principal server;		/* server's principal identifier */
    krb5_address **caddrs;		/* array of ptrs to addresses */
} krb5_kdc_rep;

/* error message structure */
typedef struct _krb5_error {
    /* some of these may be meaningless in certain contexts */
    krb5_timestamp ctime;		/* client sec portion */
    krb5_ui_2 cmsec;			/* client msec portion */
    krb5_ui_2 smsec;			/* server msec portion */
    krb5_timestamp stime;		/* server sec portion */
    krb5_ui_4 error;			/* error code (protocol error #'s) */
    krb5_principal client;		/* client's principal identifier */
    krb5_principal server;		/* client's principal identifier */
    krb5_string text;			/* descriptive text */
} krb5_error;

#endif /* __KRB5_GENERAL__ */
