#include <windows.h>
#include <stdio.h>
#include <sys/types.h>
#include <winsock2.h>
#include "leashdll.h"
#ifndef NO_KRB4
#include <KerberosIV/krb.h>
#include <prot.h>
#else
/* General definitions */
#define         KSUCCESS        0
#define         KFAILURE        255
#endif
#include <time.h>

#include <leashwin.h>
#include "leasherr.h"
#include "leash-int.h"
#include "leashids.h"

#include <mitwhich.h>

#ifndef NO_KRB4
#include <winkrbid.h>
#endif
#include "reminder.h"

static char FAR *err_context;

char KRB_HelpFile[_MAX_PATH] =	HELPFILE;

#define LEN     64                /* Maximum Hostname Length */

#define LIFE    DEFAULT_TKT_LIFE  /* lifetime of ticket in 5-minute units */

#ifndef NO_KRB4
char *
short_date(dp)
    long   *dp;
{
    register char *cp;
    cp = ctime(dp) + 4; // skip day of week
    // cp[15] = '\0';
    cp[12] = '\0'; // Don't display seconds
    return (cp);
}
#endif

static
char*
clean_string(
    char* s
    )
{
    char* p = s;
    char* b = s;

    if (!s) return s;

    for (p = s; *p; p++) {
        switch (*p) {
        case '\007':
            /* Add more cases here */
            break;
        default:
            *b = *p;
            b++;
        }
    }
    *b = *p;
    return s;
}

static
int
leash_error_message(
    const char *error,
    int rcL,
    int rc4,
    int rc5,
    int rcA,
    char* result_string,
    int  displayMB
    )
{
    char message[2048];
    char *p = message;
    int size = sizeof(message);
    int n;

    // XXX: ignore AFS for now.

    if (!rc5 && !rc4 && !rcL)
        return 0;

    n = _snprintf(p, size, "%s\n\n", error);
    p += n;
    size -= n;

    if (rc5 && !result_string)
    {
        n = _snprintf(p, size,
                      "Kerberos 5: %s (error %ld)\n",
                      perror_message(rc5),
                      rc5 & 255 // XXX: & 255??!!!
            );
        p += n;
        size -= n;
    }
    if (rc4 && !result_string)
    {
        char buffer[1024];
        n = _snprintf(p, size,
                      "Kerberos 4: %s\n",
                      err_describe(buffer, rc4)
            );
        p += n;
        size -= n;
    }
    if (rcL)
    {
        char buffer[1024];
        n = _snprintf(p, size,
                      "\n%s\n",
                      err_describe(buffer, rcL)
            );
        p += n;
        size -= n;
    }
    if (result_string)
    {
        n = _snprintf(p, size,
                      "%s\n",
                      result_string);
        p += n;
        size -= n;
    }
#ifdef USE_MESSAGE_BOX
    if ( displayMB )
        MessageBox(NULL, message, "Leash", MB_OK | MB_ICONERROR | MB_TASKMODAL |
                    MB_SETFOREGROUND);
#endif /* USE_MESSAGE_BOX */
    if (rc5) return rc5;
    if (rc4) return rc4;
    if (rcL) return rcL;
    return 0;
}


static
char *
make_postfix(
    const char * base,
    const char * postfix,
    char ** rcopy
    )
{
    int base_size;
    int ret_size;
    char * copy = 0;
    char * ret = 0;

    base_size = strlen(base) + 1;
    ret_size = base_size + strlen(postfix) + 1;
    copy = malloc(base_size);
    ret = malloc(ret_size);

    if (!copy || !ret)
        goto cleanup;

    strncpy(copy, base, base_size);
    copy[base_size - 1] = 0;

    strncpy(ret, base, base_size);
    strncpy(ret + (base_size - 1), postfix, ret_size - (base_size - 1));
    ret[ret_size - 1] = 0;

 cleanup:
    if (!copy || !ret) {
        if (copy)
            free(copy);
        if (ret)
            free(ret);
        copy = ret = 0;
    }
    // INVARIANT: (ret ==> copy) && (copy ==> ret)
    *rcopy = copy;
    return ret;
}

#ifndef NO_KRB4
static
long
make_temp_cache_v4(
    const char * postfix
    )
{
    static char * old_cache = 0;

    if (!pkrb_set_tkt_string || !ptkt_string || !pdest_tkt)
        return 0; // XXX - is this appropriate?

    if (old_cache) {
        pdest_tkt();
        pkrb_set_tkt_string(old_cache);
        free(old_cache);
        old_cache = 0;
    }

    if (postfix)
    {
        char * tmp_cache = make_postfix(ptkt_string(), postfix, &old_cache);

        if (!tmp_cache)
            return KFAILURE;

        pkrb_set_tkt_string(tmp_cache);
        free(tmp_cache);
    }
    return 0;
}
#endif

static
long
make_temp_cache_v5(
    const char * postfix,
    krb5_context * pctx
    )
{
    static krb5_context ctx = 0;
    static char * old_cache = 0;

    // INVARIANT: old_cache ==> ctx && ctx ==> old_cache

    if (pctx)
        *pctx = 0;

    if (!pkrb5_init_context || !pkrb5_free_context || !pkrb5_cc_resolve ||
        !pkrb5_cc_default_name || !pkrb5_cc_set_default_name)
        return 0;

    if (old_cache) {
        krb5_ccache cc = 0;
        if (!pkrb5_cc_resolve(ctx, pkrb5_cc_default_name(ctx), &cc))
            pkrb5_cc_destroy(ctx, cc);
        pkrb5_cc_set_default_name(ctx, old_cache);
        free(old_cache);
        old_cache = 0;
    }
    if (ctx) {
        pkrb5_free_context(ctx);
        ctx = 0;
    }

    if (postfix)
    {
        char * tmp_cache = 0;
        krb5_error_code rc = 0;

        rc = pkrb5_init_context(&ctx);
        if (rc) goto cleanup;

        tmp_cache = make_postfix(pkrb5_cc_default_name(ctx), postfix,
                                 &old_cache);

        if (!tmp_cache) {
            rc = ENOMEM;
            goto cleanup;
        }

        rc = pkrb5_cc_set_default_name(ctx, tmp_cache);

    cleanup:
        if (rc && ctx) {
            pkrb5_free_context(ctx);
            ctx = 0;
        }
        if (tmp_cache)
            free(tmp_cache);
        if (pctx)
            *pctx = ctx;
        return rc;
    }
    return 0;
}

long
Leash_checkpwd(
    char *principal,
    char *password
    )
{
    return Leash_int_checkpwd(principal, password, 0);
}

long
Leash_int_checkpwd(
    char * principal,
    char * password,
    int    displayErrors
    )
{
    long rc = 0;
	krb5_context ctx = 0;	// statically allocated in make_temp_cache_v5
    // XXX - we ignore errors in make_temp_cache_v?  This is BAD!!!
#ifndef NO_KRB4
    make_temp_cache_v4("_checkpwd");
#endif
    make_temp_cache_v5("_checkpwd", &ctx);
    rc = Leash_int_kinit_ex( ctx, 0,
							 principal, password, 0, 0, 0, 0,
							 Leash_get_default_noaddresses(),
							 Leash_get_default_publicip(),
                             displayErrors
							 );
#ifndef NO_KRB4
    make_temp_cache_v4(0);
#endif
    make_temp_cache_v5(0, &ctx);
    return rc;
}

static
long
Leash_changepwd_v5(
    char * principal,
    char * password,
    char * newpassword,
    char** error_str
    )
{
    krb5_error_code rc = 0;
    int result_code;
    krb5_data result_code_string, result_string;
    krb5_context context = 0;
    krb5_principal princ = 0;
    krb5_get_init_creds_opt opts;
    krb5_creds creds;
    DWORD addressless = 0;

    result_string.data = 0;
    result_code_string.data = 0;

    if ( !pkrb5_init_context )
        goto cleanup;

   if (rc = pkrb5_init_context(&context)) {
#if 0
       com_err(argv[0], ret, "initializing kerberos library");
#endif
       goto cleanup;
   }

   if (rc = pkrb5_parse_name(context, principal, &princ)) {
#if 0
       com_err(argv[0], ret, "parsing client name");
#endif
       goto cleanup;
   }

   pkrb5_get_init_creds_opt_init(&opts);
   pkrb5_get_init_creds_opt_set_tkt_life(&opts, 5*60);
   pkrb5_get_init_creds_opt_set_renew_life(&opts, 0);
   pkrb5_get_init_creds_opt_set_forwardable(&opts, 0);
   pkrb5_get_init_creds_opt_set_proxiable(&opts, 0);

   addressless = Leash_get_default_noaddresses();
   if (addressless)
       pkrb5_get_init_creds_opt_set_address_list(&opts,NULL);


   if (rc = pkrb5_get_init_creds_password(context, &creds, princ, password,
                                          0, 0, 0, "kadmin/changepw", &opts)) {
       if (rc == KRB5KRB_AP_ERR_BAD_INTEGRITY) {
#if 0
           com_err(argv[0], 0,
                   "Password incorrect while getting initial ticket");
#endif
       }
       else {
#if 0
           com_err(argv[0], ret, "getting initial ticket");
#endif
       }
       goto cleanup;
   }

   if (rc = pkrb5_change_password(context, &creds, newpassword,
                                  &result_code, &result_code_string,
                                  &result_string)) {
#if 0
       com_err(argv[0], ret, "changing password");
#endif
       goto cleanup;
   }

   if (result_code) {
       int len = result_code_string.length +
           (result_string.length ? (sizeof(": ") - 1) : 0) +
           result_string.length;
       if (len && error_str) {
           *error_str = malloc(len + 1);
           if (*error_str)
               _snprintf(*error_str, len + 1,
                         "%.*s%s%.*s",
                         result_code_string.length, result_code_string.data,
                         result_string.length?": ":"",
                         result_string.length, result_string.data);
       }
      rc = result_code;
      goto cleanup;
   }

 cleanup:
   if (result_string.data)
       pkrb5_free_data_contents(context, &result_string);

   if (result_code_string.data)
       pkrb5_free_data_contents(context, &result_code_string);

   if (princ)
       pkrb5_free_principal(context, princ);

   if (context)
       pkrb5_free_context(context);

   return rc;
}

#ifndef NO_KRB4
static
long
Leash_changepwd_v4(
    char * principal,
    char * password,
    char * newpassword,
    char** error_str
    )
{
    long k_errno;

    if (!pkrb_set_tkt_string || !ptkt_string || !pkadm_change_your_password ||
        !pdest_tkt)
        return KFAILURE;

    k_errno = make_temp_cache_v4("_chgpwd");
    if (k_errno) return k_errno;
    k_errno = pkadm_change_your_password(principal, password, newpassword,
                                         error_str);
    make_temp_cache_v4(0);
    return k_errno;
}
#endif

/*
 * Leash_changepwd
 *
 * Try to change the password using one of krb5 or krb4 -- whichever one
 * works.  We return ok on the first one that works.
 */
long
Leash_changepwd(
    char * principal,
    char * password,
    char * newpassword,
    char** result_string
    )
{
    return Leash_int_changepwd(principal, password, newpassword, result_string, 0);
}

long
Leash_int_changepwd(
    char * principal,
    char * password,
    char * newpassword,
    char** result_string,
    int    displayErrors
    )
{
    char* v5_error_str = 0;
    char* v4_error_str = 0;
    char* error_str = 0;
    int rc4 = 0;
    int rc5 = 0;
    int rc = 0;
    if (hKrb5)
        rc = rc5 = Leash_changepwd_v5(principal, password, newpassword,
                                      &v5_error_str);
#ifndef NO_KRB4
    if (hKrb4 &&
         Leash_get_default_use_krb4() &&
         (!hKrb5 || rc5))
        rc = rc4 = Leash_changepwd_v4(principal, password, newpassword,
                                      &v4_error_str);
#endif
    if (!rc)
        return 0;
    if (v5_error_str || v4_error_str) {
        int len = 0;
        char v5_prefix[] = "Kerberos 5: ";
        char sep[] = "\n";
        char v4_prefix[] = "Kerberos 4: ";

        clean_string(v5_error_str);
        clean_string(v4_error_str);

        if (v5_error_str)
            len += sizeof(sep) + sizeof(v5_prefix) + strlen(v5_error_str) +
                sizeof(sep);
        if (v4_error_str)
            len += sizeof(sep) + sizeof(v4_prefix) + strlen(v4_error_str) +
                sizeof(sep);
        error_str = malloc(len + 1);
        if (error_str) {
            char* p = error_str;
            int size = len + 1;
            int n;
            if (v5_error_str) {
                n = _snprintf(p, size, "%s%s%s%s",
                              sep, v5_prefix, v5_error_str, sep);
                p += n;
                size -= n;
            }
            if (v4_error_str) {
                n = _snprintf(p, size, "%s%s%s%s",
                              sep, v4_prefix, v4_error_str, sep);
                p += n;
                size -= n;
            }
            if (result_string)
                *result_string = error_str;
        }
    }
    return leash_error_message("Error while changing password.",
                               rc4, rc4, rc5, 0, error_str,
                               displayErrors
                               );
}

int (*Lcom_err)(LPSTR,long,LPSTR,...);
LPSTR (*Lerror_message)(long);
LPSTR (*Lerror_table_name)(long);


long
Leash_kinit(
    char * principal,
    char * password,
    int lifetime
    )
{
    return Leash_int_kinit_ex( 0, 0,
                               principal,
                               password,
                               lifetime,
                               Leash_get_default_forwardable(),
                               Leash_get_default_proxiable(),
                               Leash_get_default_renew_till(),
                               Leash_get_default_noaddresses(),
                               Leash_get_default_publicip(),
                               0
                               );
}

long
Leash_kinit_ex(
    char * principal,
    char * password,
    int lifetime,
    int forwardable,
    int proxiable,
    int renew_life,
    int addressless,
    unsigned long publicip
    )
{
    return Leash_int_kinit_ex( 0, /* krb5 context */
			       0, /* parent window */
                               principal,
                               password,
                               lifetime,
			       forwardable,
			       proxiable,
			       renew_life,
			       addressless,
			       publicip,
                               0
			       );
}

long
Leash_int_kinit_ex(
    krb5_context ctx,
    HWND hParent,
    char * principal,
    char * password,
    int lifetime,
    int forwardable,
    int proxiable,
    int renew_life,
    int addressless,
    unsigned long publicip,
    int displayErrors
    )
{
    LPCSTR  functionName;
    char    aname[ANAME_SZ];
    char    inst[INST_SZ];
    char    realm[REALM_SZ];
    char    first_part[256];
    char    second_part[256];
    char    temp[1024];
    int     count;
    int     i;
    int rc4 = 0;
    int rc5 = 0;
    int rcA = 0;
    int rcB = 0;
    int rcL = 0;

    if (lifetime < 5)
        lifetime = 1;
    else
        lifetime /= 5;

    if (renew_life > 0 && renew_life < 5)
	renew_life = 1;
    else
	renew_life /= 5;

    /* This should be changed if the maximum ticket lifetime */
    /* changes */

    if (lifetime > 255)
        lifetime = 255;

    err_context = "parsing principal";

    memset(temp, '\0', sizeof(temp));
    memset(inst, '\0', sizeof(inst));
    memset(realm, '\0', sizeof(realm));
    memset(first_part, '\0', sizeof(first_part));
    memset(second_part, '\0', sizeof(second_part));

    sscanf(principal, "%[/0-9a-zA-Z._-]@%[/0-9a-zA-Z._-]", first_part, second_part);
    strcpy(temp, first_part);
    strcpy(realm, second_part);
    memset(first_part, '\0', sizeof(first_part));
    memset(second_part, '\0', sizeof(second_part));
    if (sscanf(temp, "%[@0-9a-zA-Z._-]/%[@0-9a-zA-Z._-]", first_part, second_part) == 2)
    {
        strcpy(aname, first_part);
        strcpy(inst, second_part);
    }
    else
    {
        count = 0;
        i = 0;
        for (i = 0; temp[i]; i++)
        {
            if (temp[i] == '.')
                ++count;
        }
        if (count > 1)
        {
            strcpy(aname, temp);
        }
        else
        {
#ifndef NO_KRB4
            if (pkname_parse != NULL)
            {
                memset(first_part, '\0', sizeof(first_part));
                memset(second_part, '\0', sizeof(second_part));
                sscanf(temp, "%[@/0-9a-zA-Z_-].%[@/0-9a-zA-Z_-]", first_part, second_part);
                strcpy(aname, first_part);
                strcpy(inst, second_part);
            }
            else
#endif
            {
                strcpy(aname, temp);
            }
        }
    }

    memset(temp, '\0', sizeof(temp));
    strcpy(temp, aname);
    if (strlen(inst) != 0)
    {
        strcat(temp, "/");
        strcat(temp, inst);
    }
    if (strlen(realm) != 0)
    {
        strcat(temp, "@");
        strcat(temp, realm);
    }

    rc5 = Leash_krb5_kinit(ctx, hParent,
                            temp, password, lifetime,
                            forwardable,
                            proxiable,
                            renew_life,
                            addressless,
                            publicip
                            );
#ifndef NO_KRB4
    if ( Leash_get_default_use_krb4() ) {
        rc4 = KSUCCESS;

        if ( !rc5 ) {
            if (!Leash_convert524(ctx))
                rc4 = KFAILURE;
        }

        if (rc4 != KSUCCESS) {
            if (pkname_parse == NULL)
            {
                goto cleanup;
            }

            err_context = "getting realm";
            if (!*realm && (rc4  = (int)(*pkrb_get_lrealm)(realm, 1)))
            {
                functionName = "krb_get_lrealm()";
                rcL  = LSH_FAILEDREALM;
                goto cleanup;
            }

            err_context = "checking principal";
            if ((!*aname) || (!(rc4  = (int)(*pk_isname)(aname))))
            {
                functionName = "krb_get_lrealm()";
                rcL = LSH_INVPRINCIPAL;
                goto cleanup;
            }

            /* optional instance */
            if (!(rc4 = (int)(*pk_isinst)(inst)))
            {
                functionName = "k_isinst()";
                rcL = LSH_INVINSTANCE;
                goto cleanup;
            }

            if (!(rc4 = (int)(*pk_isrealm)(realm)))
            {
                functionName = "k_isrealm()";
                rcL = LSH_INVREALM;
                goto cleanup;
            }

            err_context = "fetching ticket";
            rc4 = (*pkrb_get_pw_in_tkt)(aname, inst, "", "krbtgt", realm,
                                         lifetime, password);
            if (rc4) /* XXX: do we want: && (rc != NO_TKT_FIL) as well? */
            {
                functionName = "krb_get_pw_in_tkt()";
                rcL = KRBERR(rc4);
                goto cleanup;
            }
        }
    }
#endif
#ifndef NO_AFS
    if ( !rc5 || (Leash_get_default_use_krb4() && !rc4) ) {
        char c;
        char *r;
        char *t;
        for ( r=realm, t=temp; c=*r; r++,t++ )
            *t = isupper(c) ? tolower(c) : c;
        *t = '\0';

        rcA = Leash_afs_klog("afs", temp, "", lifetime);
        rcB = Leash_afs_klog("afs", "", "", lifetime);
        if (!(rcA && rcB))
            rcA = 0;
        else if (!rcA)
            rcA = rcB;
    }
#endif /* NO_AFS */

 cleanup:
    return leash_error_message("Ticket initialization failed.",
                               rcL,
#ifdef NO_KRB4
                               0,
#else
                               (rc5 && rc4)?KRBERR(rc4):0,
#endif
                               rc5, rcA, 0,
                               displayErrors);
}

long FAR
Leash_renew(void)
{
    if ( hKrb5 && !LeashKRB5_renew() ) {
        int lifetime;
        lifetime = Leash_get_default_lifetime() / 5;
#ifndef NO_KRB4
        if (hKrb4 && Leash_get_default_use_krb4())
            Leash_convert524(0);
#endif
#ifndef NO_AFS
        {
            TicketList * list = NULL, * token;
            not_an_API_LeashAFSGetToken(NULL,&list,NULL);
            for ( token = list ; token ; token = token->next )
                Leash_afs_klog("afs", token->realm, "", lifetime);
            not_an_API_LeashFreeTicketList(&list);
        }
#endif /* NO_AFS */
        return 1;
    }
    return 0;
}

BOOL
GetSecurityLogonSessionData(PSECURITY_LOGON_SESSION_DATA * ppSessionData)
{
    NTSTATUS Status = 0;
    HANDLE  TokenHandle;
    TOKEN_STATISTICS Stats;
    DWORD   ReqLen;
    BOOL    Success;
    PSECURITY_LOGON_SESSION_DATA pSessionData;

    if (!ppSessionData)
        return FALSE;
    *ppSessionData = NULL;

    Success = OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &TokenHandle );
    if ( !Success )
        return FALSE;

    Success = GetTokenInformation( TokenHandle, TokenStatistics, &Stats, sizeof(TOKEN_STATISTICS), &ReqLen );
    CloseHandle( TokenHandle );
    if ( !Success )
        return FALSE;

    Status = pLsaGetLogonSessionData( &Stats.AuthenticationId, &pSessionData );
    if ( FAILED(Status) || !pSessionData )
        return FALSE;

	*ppSessionData = pSessionData;
    return TRUE;
}

// IsKerberosLogon() does not validate whether or not there are valid tickets in the
// cache.  It validates whether or not it is reasonable to assume that if we
// attempted to retrieve valid tickets we could do so.  Microsoft does not
// automatically renew expired tickets.  Therefore, the cache could contain
// expired or invalid tickets.  Microsoft also caches the user's password
// and will use it to retrieve new TGTs if the cache is empty and tickets
// are requested.

BOOL
IsKerberosLogon(VOID)
{
    PSECURITY_LOGON_SESSION_DATA pSessionData = NULL;
    BOOL    Success = FALSE;

    if ( GetSecurityLogonSessionData(&pSessionData) ) {
        if ( pSessionData->AuthenticationPackage.Buffer ) {
            WCHAR buffer[256];
            WCHAR *usBuffer;
            int usLength;

            Success = FALSE;
            usBuffer = (pSessionData->AuthenticationPackage).Buffer;
            usLength = (pSessionData->AuthenticationPackage).Length;
            if (usLength < 256)
            {
                lstrcpynW (buffer, usBuffer, usLength);
                lstrcatW (buffer,L"");
                if ( !lstrcmpW(L"Kerberos",buffer) )
                    Success = TRUE;
            }
        }
        pLsaFreeReturnBuffer(pSessionData);
    }
    return Success;
}

static BOOL
IsWindowsVista (void)
{
    static BOOL fChecked = FALSE;
    static BOOL fIsVista = FALSE;

    if (!fChecked)
    {
        OSVERSIONINFO Version;

        memset (&Version, 0x00, sizeof(Version));
        Version.dwOSVersionInfoSize = sizeof(Version);

        if (GetVersionEx (&Version))
        {
            if (Version.dwPlatformId == VER_PLATFORM_WIN32_NT && Version.dwMajorVersion >= 6)
                fIsVista = TRUE;
        }
        fChecked = TRUE;
    }

    return fIsVista;
}

static BOOL
IsProcessUacLimited (void)
{
    static BOOL fChecked = FALSE;
    static BOOL fIsUAC = FALSE;

    if (!fChecked)
    {
        NTSTATUS Status = 0;
        HANDLE  TokenHandle;
        DWORD   ElevationLevel;
        DWORD   ReqLen;
        BOOL    Success;

        if (IsWindowsVista()) {
            Success = OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &TokenHandle );
            if ( Success ) {
                Success = GetTokenInformation( TokenHandle,
                                               TokenOrigin+1 /* ElevationLevel */,
                                               &ElevationLevel, sizeof(DWORD), &ReqLen );
                CloseHandle( TokenHandle );
                if ( Success && ElevationLevel == 3 /* Limited */ )
                    fIsUAC = TRUE;
            }
        }
        fChecked = TRUE;
    }
    return fIsUAC;

}

// This looks really ugly because it is.  The result of IsKerberosLogon()
// does not prove whether or not there are Kerberos tickets available to
// be imported.  Only the call to Leash_ms2mit() which actually attempts
// to import tickets can do that.  However, calling Leash_ms2mit() can
// result in a TGS_REQ being sent to the KDC and since Leash_importable()
// is called quite often we want to avoid this if at all possible.
// Unfortunately, we have be shown at least one case in which the primary
// authentication package was not Kerberos and yet there were Kerberos
// tickets available.  Therefore, if IsKerberosLogon() is not TRUE we
// must call Leash_ms2mit() but we still do not want to call it in a
// tight loop so we cache the response and assume it won't change.

// 2007-03-21
// And the nightmare goes on.  On Vista the Lsa call we use to determine
// whether or not Kerberos was used for logon fails to return and worse
// corrupts the stack.  Therefore, we must now test to see if the
// operating system is Vista and skip the call to IsKerberosLogon()
// if it is.
long FAR
Leash_importable(void)
{
    if (IsProcessUacLimited())
	return FALSE;

    if ( !IsWindowsVista() && IsKerberosLogon() )
        return TRUE;
    else {
        static int response = -1;
        if (response == -1) {
            response = Leash_ms2mit(0);
        }
        return response;
    }
}

long FAR
Leash_import(void)
{
    if ( Leash_ms2mit(1) ) {
        int lifetime;
        lifetime = Leash_get_default_lifetime() / 5;
#ifndef NO_KRB4
        if (hKrb4 && Leash_get_default_use_krb4())
            Leash_convert524(0);
#endif
#ifndef NO_AFS
        {
            char c;
            char *r;
            char *t;
            char  cell[256];
            char  realm[256];
            int   i = 0;
            int   rcA = 0;
            int   rcB = 0;

            krb5_context ctx = 0;
            krb5_error_code code = 0;
            krb5_ccache cc = 0;
            krb5_principal me = 0;

            if ( !pkrb5_init_context )
                goto cleanup;

            code = pkrb5_init_context(&ctx);
            if (code) goto cleanup;

            code = pkrb5_cc_default(ctx, &cc);
            if (code) goto cleanup;

            if (code = pkrb5_cc_get_principal(ctx, cc, &me))
                goto cleanup;

            for ( r=realm, t=cell, i=0; i<krb5_princ_realm(ctx, me)->length; r++,t++,i++ ) {
                c = krb5_princ_realm(ctx, me)->data[i];
                *r = c;
                *t = isupper(c) ? tolower(c) : c;
            }
            *r = *t = '\0';

            rcA = Leash_afs_klog("afs", cell, "", lifetime);
            rcB = Leash_afs_klog("afs", "", "", lifetime);
            if (!(rcA && rcB))
                rcA = 0;
            else if (!rcA)
                rcA = rcB;

          cleanup:
            if (me)
                pkrb5_free_principal(ctx, me);
            if (cc)
                pkrb5_cc_close(ctx, cc);
            if (ctx)
                pkrb5_free_context(ctx);
        }
#endif /* NO_AFS */
        return 1;
    }
    return 0;
}

long
Leash_kdestroy(void)
{
#ifdef NO_KRB4
    return 0;
#else
    int k_errno;

    Leash_afs_unlog();
    Leash_krb5_kdestroy();

    if (pdest_tkt != NULL)
    {
        k_errno = (*pdest_tkt)();
        if (k_errno && (k_errno != RET_TKFIL))
            return KRBERR(k_errno);
    }

    return 0;
#endif
}

#ifndef NO_KRB4
int com_addr(void)
{
    long ipAddr;
    char loc_addr[ADDR_SZ];
    CREDENTIALS cred;
    char service[40];
    char instance[40];
//    char addr[40];
    char realm[40];
    struct in_addr LocAddr;
    int k_errno;

    if (pkrb_get_cred == NULL)
        return(KSUCCESS);

    k_errno = (*pkrb_get_cred)(service,instance,realm,&cred);
    if (k_errno)
        return KRBERR(k_errno);


    while(1) {
	ipAddr = (*pLocalHostAddr)();
	LocAddr.s_addr = ipAddr;
        strcpy(loc_addr,inet_ntoa(LocAddr));
	if ( strcmp(cred.address,loc_addr) != 0) {
            Leash_kdestroy ();
            break;
	}
        break;
    } // while()
    return 0;
}
#endif

long FAR
not_an_API_LeashFreeTicketList(TicketList** ticketList)
{
    TicketList* tempList = *ticketList, *killList;

    //if (tempList == NULL)
    //return -1;

    while (tempList)
    {
        killList = tempList;

        tempList = (TicketList*)tempList->next;
        free(killList->theTicket);
        if (killList->tktEncType)
            free(killList->tktEncType);
        if (killList->keyEncType)
            free(killList->keyEncType);
        if (killList->addrCount) {
            int n;
            for ( n=0; n<killList->addrCount; n++) {
                if (killList->addrList[n])
                    free(killList->addrList[n]);
            }
        }
        if (killList->addrList)
            free(killList->addrList);
        if (killList->name)
            free(killList->name);
        if (killList->inst)
            free(killList->inst);
        if (killList->realm)
            free(killList->realm);
        free(killList);
    }

    *ticketList = NULL;
    return 0;
}

long
not_an_API_LeashKRB4GetTickets(TICKETINFO FAR* ticketinfo,
                               TicketList** ticketList)
{
#ifdef NO_KRB4
    return(KFAILURE);
#else
    // Puts tickets in a returned linklist - Can be used with many
    // diff. controls
    char    pname[ANAME_SZ];
    char    pinst[INST_SZ];
    char    prealm[REALM_SZ];
    char    buf[MAX_K_NAME_SZ+40];
    LPSTR   cp;
    LPSTR   functionName;
    long    expdate;
    int     k_errno;
    CREDENTIALS c;
    int newtickets = 0;
    int open = 0;

    TicketList* list = NULL;
    if ( ticketinfo ) {
        ticketinfo->btickets = NO_TICKETS;
        ticketinfo->principal[0] = '\0';
    }

    // Since krb_get_tf_realm will return a ticket_file error,
    // we will call tf_init and tf_close first to filter out
    // things like no ticket file.  Otherwise, the error that
    // the user would see would be
    // klist: can't find realm of ticket file: No ticket file (tf_util)
    // instead of klist: No ticket file (tf_util)
    if (ptf_init == NULL)
        return(KSUCCESS);

    com_addr();
    err_context = (LPSTR)"tktf1";

    // Open ticket file
    if ((k_errno = (*ptf_init)((*ptkt_string)(), R_TKT_FIL)))
    {
        functionName = "ptf_init()";
        goto cleanup;
    }
    // Close ticket file
    (void) (*ptf_close)();

    // We must find the realm of the ticket file here before calling
    // tf_init because since the realm of the ticket file is not
    // really stored in the principal section of the file, the
    // routine we use must itself call tf_init and tf_close.

    err_context = "tf realm";
    if ((k_errno = (*pkrb_get_tf_realm)((*ptkt_string)(), prealm)) != KSUCCESS)
    {
        functionName = "pkrb_get_tf_realm()";
        goto cleanup;
    }

    // Open ticket file
    err_context = "tf init";
    if (k_errno = (*ptf_init)((*ptkt_string)(), R_TKT_FIL))
    {
        functionName = "sptf_init()";
        goto cleanup;
    }

    open = 1;
    err_context = "tf pname";

    // Get principal name and instance
    if ((k_errno = (*ptf_get_pname)(pname)) || (k_errno = (*ptf_get_pinst)(pinst)))
    {
        functionName = "ptf_get_pname()";
        goto cleanup;
    }

    // You may think that this is the obvious place to get the
    // realm of the ticket file, but it can't be done here as the
    // routine to do this must open the ticket file.  This is why
    // it was done before tf_init.
    wsprintf((LPSTR)ticketinfo->principal,"%s%s%s%s%s", (LPSTR)pname,
             (LPSTR)(pinst[0] ? "." : ""), (LPSTR)pinst,
             (LPSTR)(prealm[0] ? "@" : ""), (LPSTR)prealm);

    newtickets = NO_TICKETS;
    err_context = "tf cred";

    // Get KRB4 tickets
    while ((k_errno = (*ptf_get_cred)(&c)) == KSUCCESS)
    {
        if (!list)
        {
            list = (TicketList*) calloc(1, sizeof(TicketList));
            (*ticketList) = list;
        }
        else
        {
            list->next = (struct TicketList*) calloc(1, sizeof(TicketList));
            list = (TicketList*) list->next;
        }

        expdate = c.issue_date + c.lifetime * 5L * 60L;

        if (!lstrcmp((LPSTR)c.service, (LPSTR)TICKET_GRANTING_TICKET) && !lstrcmp((LPSTR)c.instance, (LPSTR)prealm))
        {
            ticketinfo->issue_date = c.issue_date;
            ticketinfo->lifetime = c.lifetime * 5L * 60L;
            ticketinfo->renew_till = 0;
        }

        _tzset();
        if ( ticketinfo->issue_date + ticketinfo->lifetime - time(0) <= 0L )
            newtickets = EXPD_TICKETS;
        else
            newtickets = GOOD_TICKETS;

        cp = (LPSTR)buf;
        cp += wsprintf(cp, "%s     ",
                       short_date(&c.issue_date));
        wsprintf(cp, "%s     %s%s%s%s%s",
                 short_date(&expdate),
                 c.service,
                 (c.instance[0] ? "." : ""),
                 c.instance,
                 (c.realm[0] ? "@" : ""),
                 c.realm);

        list->theTicket = (char*) calloc(1, sizeof(buf));
        if (!list->theTicket)
        {
#ifdef USE_MESSAGE_BOX
            MessageBox(NULL, "Memory Error", "Error", MB_OK);
#endif /* USE_MESSAGE_BOX */
            return ENOMEM;
        }

        strcpy(list->theTicket, buf);
        list->name = NULL;
        list->inst = NULL;
        list->realm = NULL;
        list->tktEncType = NULL;
        list->keyEncType = NULL;
        list->addrCount = 0;
        list->addrList = NULL;

    } // while
    functionName = "not_an_API_LeashKRB4GetTickets()";

cleanup:
    if (ptf_close == NULL)
        return(KSUCCESS);

    if (open)
        (*ptf_close)(); //close ticket file

    if (k_errno == EOF)
        k_errno = 0;

    // XXX the if statement directly below was inserted to eliminate
    // an error NO_TKT_FIL on Leash startup. The error occurs from an
    // error number thrown from krb_get_tf_realm.  We believe this
    // change does not eliminate other errors, but it may.

    if (k_errno == NO_TKT_FIL)
        k_errno = 0;

    ticketinfo->btickets = newtickets;

#ifdef USE_MESSAGE_BOX
    if (k_errno)
    {
        CHAR message[256];
        CHAR errBuf[256];
        LPCSTR errText;

        if (!Lerror_message)
            return -1;

        errText = err_describe(errBuf, KRBERR(k_errno));

        sprintf(message, "%s\n\n%s failed", errText, functionName);
        MessageBox(NULL, message, "Kerberos Four",
                   MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
    }
#endif /* USE_MESSAGE_BOX */
    return k_errno;
#endif
}

long FAR Leash_klist(HWND hlist, TICKETINFO FAR *ticketinfo)
{
#ifdef NO_KRB4
    return(KFAILURE);
#else
    // Don't think this function will be used anymore - ADL 5-15-99
    // Old fucntion to put tickets in a listbox control
    // Use function  "not_an_API_LeashKRB4GetTickets()" instead!
    char    pname[ANAME_SZ];
    char    pinst[INST_SZ];
    char    prealm[REALM_SZ];
    char    buf[MAX_K_NAME_SZ+40];
    LPSTR   cp;
    long    expdate;
    int     k_errno;
    CREDENTIALS c;
    int newtickets = 0;
    int open = 0;

    /*
     * Since krb_get_tf_realm will return a ticket_file error,
     * we will call tf_init and tf_close first to filter out
     * things like no ticket file.  Otherwise, the error that
     * the user would see would be
     * klist: can't find realm of ticket file: No ticket file (tf_util)
     * instead of
     * klist: No ticket file (tf_util)
     */
    if (ptf_init == NULL)
        return(KSUCCESS);

    if (hlist)
    {
        SendMessage(hlist, WM_SETREDRAW, FALSE, 0L);
        SendMessage(hlist, LB_RESETCONTENT, 0, 0L);
    }
    com_addr();
    newtickets = NO_TICKETS;

    err_context = (LPSTR)"tktf1";

    /* Open ticket file */
    if (k_errno = (*ptf_init)((*ptkt_string)(), R_TKT_FIL))
    {
        goto cleanup;
    }
    /* Close ticket file */
    (void) (*ptf_close)();
    /*
     * We must find the realm of the ticket file here before calling
     * tf_init because since the realm of the ticket file is not
     * really stored in the principal section of the file, the
     * routine we use must itself call tf_init and tf_close.
     */
    err_context = "tf realm";
    if ((k_errno = (*pkrb_get_tf_realm)((*ptkt_string)(), prealm)) != KSUCCESS)
    {
        goto cleanup;
    }
    /* Open ticket file */
    err_context = "tf init";
    if (k_errno = (*ptf_init)((*ptkt_string)(), R_TKT_FIL))
    {
        goto cleanup;
    }

    open = 1;
    err_context = "tf pname";
    /* Get principal name and instance */
    if ((k_errno = (*ptf_get_pname)(pname)) || (k_errno = (*ptf_get_pinst)(pinst)))
    {
        goto cleanup;
    }

    /*
     * You may think that this is the obvious place to get the
     * realm of the ticket file, but it can't be done here as the
     * routine to do this must open the ticket file.  This is why
     * it was done before tf_init.
     */

    wsprintf((LPSTR)ticketinfo->principal,"%s%s%s%s%s", (LPSTR)pname,
             (LPSTR)(pinst[0] ? "." : ""), (LPSTR)pinst,
             (LPSTR)(prealm[0] ? "@" : ""), (LPSTR)prealm);
    newtickets = GOOD_TICKETS;

    err_context = "tf cred";
    while ((k_errno = (*ptf_get_cred)(&c)) == KSUCCESS)
    {
        expdate = c.issue_date + c.lifetime * 5L * 60L;

        if (!lstrcmp((LPSTR)c.service, (LPSTR)TICKET_GRANTING_TICKET) && !lstrcmp((LPSTR)c.instance, (LPSTR)prealm))
        {
            ticketinfo->issue_date = c.issue_date;
            ticketinfo->lifetime = c.lifetime * 5L * 60L;
            ticketinfo->renew_till = 0;
        }

        cp = (LPSTR)buf;
        lstrcpy(cp, (LPSTR)short_date(&c.issue_date));
        cp += lstrlen(cp);
        wsprintf(cp,"\t%s\t%s%s%s%s%s",
                 (LPSTR)short_date(&expdate), (LPSTR)c.service,
                 (LPSTR)(c.instance[0] ? "." : ""),
                 (LPSTR)c.instance, (LPSTR)(c.realm[0] ? "@" : ""),
                 (LPSTR) c.realm);
        if (hlist)
            SendMessage(hlist, LB_ADDSTRING, 0, (LONG)(LPSTR)buf);
    } /* WHILE */

cleanup:

    if (open)
        (*ptf_close)(); /* close ticket file */

    if (hlist)
    {
        SendMessage(hlist, WM_SETREDRAW, TRUE, 0L);
        InvalidateRect(hlist, NULL, TRUE);
        UpdateWindow(hlist);
    }
    if (k_errno == EOF)
        k_errno = 0;

    /* XXX the if statement directly below was inserted to eliminate
       an error 20 on Leash startup. The error occurs from an error
       number thrown from krb_get_tf_realm.  We believe this change
       does not eliminate other errors, but it may. */

    if (k_errno == RET_NOTKT)
        k_errno = 0;

    ticketinfo->btickets = newtickets;
    if (k_errno != 0)
        return KRBERR(k_errno);
    return 0;
#endif
}


// This function can be used to set the help file that will be
// referenced the DLL's PasswordProcDLL function and err_describe
// function.  Returns true if the help file has been set to the
// argument or the environment variable KERB_HELP. Returns FALSE if
// the default helpfile as defined in by HELPFILE in lsh_pwd.h is
// used.
BOOL Leash_set_help_file( char *szHelpFile )
{
    char tmpHelpFile[256];
    BOOL ret = 0;

    if( szHelpFile == NULL ){
	GetEnvironmentVariable("KERB_HELP", tmpHelpFile, sizeof(tmpHelpFile));
    } else {
	strcpy( KRB_HelpFile, szHelpFile );
	ret++;
    }

    if( !ret && tmpHelpFile[0] ){
	strcpy( KRB_HelpFile, tmpHelpFile );
	ret++;
    }

    if( !ret){
	strcpy( KRB_HelpFile, HELPFILE );
    }

    return(ret);
}



LPSTR Leash_get_help_file(void)
{
    return( KRB_HelpFile);
}

#if 0
/**************************************/
/* LeashKrb4ErrorMessage():           */
/**************************************/
long
LeashKrb4ErrorMessage(LONG rc, LPCSTR FailedFunctionName)
{
    // At this time the Leashw32.dll. takes care of all error messages. We
    // may want to add a flag latter on so the .exe can handle it's own
    // errors.

    CHAR message[256];
    CHAR errBuf[256];
    LPCSTR errText;

    if (!Lerror_message)
      return -1;

    errText = err_describe(errBuf, rc);

    sprintf(message, "%s\n\n%s failed", errText, FailedFunctionName);
    MessageBox(NULL, message, "Kerberos Four", MB_OK |
                                               MB_ICONERROR |
                                               MB_TASKMODAL |
                                               MB_SETFOREGROUND);
    return rc;
}
#endif

int
Leash_debug(
    int class,
    int priority,
    char* fmt, ...
    )
{

    return 0;
}


static int
get_profile_file(LPSTR confname, UINT szConfname)
{
    char **configFile = NULL;
    if (hKrb5) {
        if (pkrb5_get_default_config_files(&configFile))
        {
            GetWindowsDirectory(confname,szConfname);
            confname[szConfname-1] = '\0';
            strncat(confname,"\\KRB5.INI",szConfname-strlen(confname));
            confname[szConfname-1] = '\0';
            return FALSE;
        }

        *confname = 0;

        if (configFile)
        {
            strncpy(confname, *configFile, szConfname);
            confname[szConfname-1] = '\0';
            pkrb5_free_config_files(configFile);
        }
    }

    if (!*confname)
    {
        GetWindowsDirectory(confname,szConfname);
        confname[szConfname-1] = '\0';
        strncat(confname,"\\KRB5.INI",szConfname-strlen(confname));
        confname[szConfname-1] = '\0';
    }

    return FALSE;
}

static const char *const conf_yes[] = {
    "y", "yes", "true", "t", "1", "on",
    0,
};

static const char *const conf_no[] = {
    "n", "no", "false", "nil", "0", "off",
    0,
};

int
config_boolean_to_int(const char *s)
{
    const char *const *p;

    for(p=conf_yes; *p; p++) {
        if (!strcasecmp(*p,s))
            return 1;
    }

    for(p=conf_no; *p; p++) {
        if (!strcasecmp(*p,s))
            return 0;
    }

    /* Default to "no" */
    return 0;
}

/*
 * Leash_get_default_lifetime:
 *
 * This function is used to get the default ticket lifetime for this
 * process in minutes.  A return value of 0 indicates no setting or
 * "default" setting obtained.
 *
 * Here is where we look in order:
 *
 * - LIFETIME environment variable
 * - HKCU\Software\MIT\Leash,lifetime
 * - HKLM\Software\MIT\Leash,lifetime
 * - string resource in the leash DLL
 */

BOOL
get_DWORD_from_registry(
    HKEY hBaseKey,
    char * key,
    char * value,
    DWORD * result
    )
{
    HKEY hKey;
    DWORD dwCount;
    LONG rc;

    rc = RegOpenKeyEx(hBaseKey, key, 0, KEY_QUERY_VALUE, &hKey);
    if (rc)
        return FALSE;

    dwCount = sizeof(DWORD);
    rc = RegQueryValueEx(hKey, value, 0, 0, (LPBYTE) result, &dwCount);
    RegCloseKey(hKey);

    return rc?FALSE:TRUE;
}

BOOL
get_STRING_from_registry(
    HKEY hBaseKey,
    char * key,
    char * value,
    char * outbuf,
    DWORD  outlen
    )
{
    HKEY hKey;
    DWORD dwCount;
    LONG rc;

	if (!outbuf || outlen == 0)
		return FALSE;

    rc = RegOpenKeyEx(hBaseKey, key, 0, KEY_QUERY_VALUE, &hKey);
    if (rc)
        return FALSE;

    dwCount = outlen;
    rc = RegQueryValueEx(hKey, value, 0, 0, (LPBYTE) outbuf, &dwCount);
    RegCloseKey(hKey);

    return rc?FALSE:TRUE;
}

static
BOOL
get_default_lifetime_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_LIFETIME,
                                   result);
}

DWORD
Leash_reset_default_lifetime(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_LIFETIME);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_lifetime(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_LIFETIME, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_lifetime(
    )
{
    HMODULE hmLeash;
    char env[32];
    DWORD result;


    if (GetEnvironmentVariable("LIFETIME",env,sizeof(env)))
    {
        return atoi(env);
    }


    if (get_default_lifetime_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_lifetime_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
	return result;
    }

    if ( hKrb5 ) {
        CHAR confname[MAX_PATH];

        if (!get_profile_file(confname, sizeof(confname)))
        {
            profile_t profile;
            const char *filenames[2];
            long retval;

            filenames[0] = confname;
            filenames[1] = NULL;
            if (!pprofile_init(filenames, &profile)) {
                char * value = NULL;

                retval = pprofile_get_string(profile, "libdefaults", "ticket_lifetime", NULL, NULL, &value);
                if (retval == 0 && value) {
                    krb5_deltat d;

		    retval = pkrb5_string_to_deltat(value, &d);

                    if (retval == KRB5_DELTAT_BADFORMAT) {
                        /* Historically some sites use relations of
                           the form 'ticket_lifetime = 24000' where
                           the unit is left out but is assumed to be
                           seconds. Then there are other sites which
                           use the form 'ticket_lifetime = 600' where
                           the unit is assumed to be minutes.  While
                           these are technically wrong (a unit needs
                           to be specified), we try to accomodate for
                           this using the safe assumption that the
                           unit is seconds and tack an 's' to the end
                           and see if that works. */

			/* Of course, Leash is one of the platforms
			   that historically assumed no units and minutes
			   so this change is going to break some people
			   but its better to be consistent. */
                        size_t cch;
                        char buf[256];

			do {
			    cch = strlen(value) + 2; /* NUL and new 's' */
			    if (cch > sizeof(buf))
				break;

			    strcpy(buf, value);
			    strcat(buf, "s");

			    retval = pkrb5_string_to_deltat(buf, &d);

			    if (retval == 0) {
				result = d / 60;
			    }
			} while(0);
                    } else if (retval == 0) {
                        result = d / 60;
                    }

                    pprofile_release_string(value);
                }
                pprofile_release(profile);
		/* value has been released but we can still use a check for
		 * non-NULL to see if we were able to read a value.
		 */
		if (retval == 0 && value)
		    return result;
            }
        }
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char lifetime[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_LIFE,
                       lifetime, sizeof(lifetime)))
        {
            lifetime[sizeof(lifetime) - 1] = 0;
            return atoi(lifetime);
        }
    }
    return 0;
}

static
BOOL
get_default_renew_till_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_RENEW_TILL,
                                   result);
}

DWORD
Leash_reset_default_renew_till(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_RENEW_TILL);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_renew_till(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_RENEW_TILL, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_renew_till(
    )
{
    HMODULE hmLeash;
    char env[32];
    DWORD result;

    if(GetEnvironmentVariable("RENEW_TILL",env,sizeof(env)))
    {
        return atoi(env);
    }

    if (get_default_renew_till_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_renew_till_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    if ( hKrb5 ) {
        CHAR confname[MAX_PATH];
        if (!get_profile_file(confname, sizeof(confname)))
        {
            profile_t profile;
            const char *filenames[2];
            int value=0;
            long retval;
            filenames[0] = confname;
            filenames[1] = NULL;

	    if (!pprofile_init(filenames, &profile)) {
                char * value = NULL;

		retval = pprofile_get_string(profile, "libdefaults", "renew_lifetime", NULL, NULL, &value);
                if (retval == 0 && value) {
                    krb5_deltat d;

		    retval = pkrb5_string_to_deltat(value, &d);
		    if (retval == KRB5_DELTAT_BADFORMAT) {
                        /* Historically some sites use relations of
                           the form 'ticket_lifetime = 24000' where
                           the unit is left out but is assumed to be
                           seconds. Then there are other sites which
                           use the form 'ticket_lifetime = 600' where
                           the unit is assumed to be minutes.  While
                           these are technically wrong (a unit needs
                           to be specified), we try to accomodate for
                           this using the safe assumption that the
                           unit is seconds and tack an 's' to the end
                           and see if that works. */

			/* Of course, Leash is one of the platforms
			   that historically assumed no units and minutes
			   so this change is going to break some people
			   but its better to be consistent. */
                        size_t cch;
                        char buf[256];
			do {
			    cch = strlen(value) + 2; /* NUL and new 's' */
			    if (cch > sizeof(buf))
				break;

			    strcpy(buf, value);
			    strcat(buf, "s");

			    retval = pkrb5_string_to_deltat(buf, &d);
			    if (retval == 0) {
				result = d / 60;
			    }
			} while(0);
                    } else if (retval == 0) {
			result = d / 60;
                    }
                    pprofile_release_string(value);
                }
		pprofile_release(profile);
		/* value has been released but we can still use a check for
		 * non-NULL to see if we were able to read a value.
		 */
		if (retval == 0 && value)
		    return result;

		pprofile_release(profile);
            }
        }
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char renew_till[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_RENEW_TILL,
                       renew_till, sizeof(renew_till)))
        {
            renew_till[sizeof(renew_till) - 1] = 0;
            return atoi(renew_till);
        }
    }
    return 0;
}

static
BOOL
get_default_forwardable_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_FORWARDABLE,
                                   result);
}

DWORD
Leash_reset_default_forwardable(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_FORWARDABLE);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_forwardable(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_FORWARDABLE, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_forwardable(
    )
{
    HMODULE hmLeash;

    char env[32];
    DWORD result;

    if(GetEnvironmentVariable("FORWARDABLE",env,sizeof(env)))
    {
        return atoi(env);
    }

    if (get_default_forwardable_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_forwardable_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    if ( hKrb5 ) {
        CHAR confname[MAX_PATH];
        if (!get_profile_file(confname, sizeof(confname)))
        {
            profile_t profile;
            const char *filenames[2];
            char *value=0;
            long retval;
            filenames[0] = confname;
            filenames[1] = NULL;
            if (!pprofile_init(filenames, &profile)) {
                retval = pprofile_get_string(profile, "libdefaults","forwardable", 0, 0, &value);
                if ( value ) {
                    result = config_boolean_to_int(value);
                    pprofile_release_string(value);
                    pprofile_release(profile);
                    return result;
                }
                pprofile_release(profile);
            }
        }
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char forwardable[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_FORWARD,
                       forwardable, sizeof(forwardable)))
        {
            forwardable[sizeof(forwardable) - 1] = 0;
            return atoi(forwardable);
        }
    }
    return 0;
}

static
BOOL
get_default_renewable_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_RENEWABLE,
                                   result);
}

DWORD
Leash_reset_default_renewable(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_RENEWABLE);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_renewable(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_RENEWABLE, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_renewable(
    )
{
    HMODULE hmLeash;
    char env[32];
    DWORD result;

    if(GetEnvironmentVariable("RENEWABLE",env,sizeof(env)))
    {
        return atoi(env);
    }

    if (get_default_renewable_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_renewable_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    if ( hKrb5 ) {
        CHAR confname[MAX_PATH];
        if (!get_profile_file(confname, sizeof(confname)))
        {
            profile_t profile;
            const char *filenames[2];
            char *value=0;
            long retval;
            filenames[0] = confname;
            filenames[1] = NULL;
            if (!pprofile_init(filenames, &profile)) {
                retval = pprofile_get_string(profile, "libdefaults","renewable", 0, 0, &value);
                if ( value ) {
                    result = config_boolean_to_int(value);
                    pprofile_release_string(value);
                    pprofile_release(profile);
                    return result;
                }
                pprofile_release(profile);
            }
        }
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char renewable[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_RENEW,
                       renewable, sizeof(renewable)))
        {
            renewable[sizeof(renewable) - 1] = 0;
            return atoi(renewable);
        }
    }
    return 0;
}

static
BOOL
get_default_noaddresses_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_NOADDRESSES,
                                   result);
}

DWORD
Leash_reset_default_noaddresses(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_NOADDRESSES);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_noaddresses(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_NOADDRESSES, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_noaddresses(
    )
{
    HMODULE hmLeash;
    char env[32];
    DWORD result;

    if ( hKrb5 ) {
        // if the profile file cannot be opened then the value will be true
        // if the noaddresses name cannot be found then the value will be true
        // if true in the library, we can't alter it by other means
        CHAR confname[MAX_PATH];
        result = 1;
        if (!get_profile_file(confname, sizeof(confname)))
        {
            profile_t profile;
            const char *filenames[2];
            char *value=0;
            long retval;
            filenames[0] = confname;
            filenames[1] = NULL;
            if (!pprofile_init(filenames, &profile)) {
                retval = pprofile_get_string(profile, "libdefaults","noaddresses", 0, "true", &value);
                if ( value ) {
                    result = config_boolean_to_int(value);
                    pprofile_release_string(value);
                }
                pprofile_release(profile);
            }
        }

        if ( result )
            return 1;
    }

    // The library default is false, check other locations

    if(GetEnvironmentVariable("NOADDRESSES",env,sizeof(env)))
    {
        return atoi(env);
    }

    if (get_default_noaddresses_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_noaddresses_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char noaddresses[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_NOADDRESS,
                       noaddresses, sizeof(noaddresses)))
        {
            noaddresses[sizeof(noaddresses) - 1] = 0;
        }
    }
    return 1;
}

static
BOOL
get_default_proxiable_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_PROXIABLE,
                                   result);
}

DWORD
Leash_reset_default_proxiable(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_PROXIABLE);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_proxiable(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_PROXIABLE, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_proxiable(
    )
{
    HMODULE hmLeash;
    char env[32];
    DWORD result;

    if(GetEnvironmentVariable("PROXIABLE",env,sizeof(env)))
    {
        return atoi(env);
    }

    if (get_default_proxiable_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_proxiable_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    if ( hKrb5 ) {
        CHAR confname[MAX_PATH];
        if (!get_profile_file(confname, sizeof(confname)))
        {
            profile_t profile;
            const char *filenames[2];
            char *value=0;
            long retval;
            filenames[0] = confname;
            filenames[1] = NULL;
            if (!pprofile_init(filenames, &profile)) {
                retval = pprofile_get_string(profile, "libdefaults","proxiable", 0, 0, &value);
                if ( value ) {
                    result = config_boolean_to_int(value);
                    pprofile_release_string(value);
                    pprofile_release(profile);
                    return result;
                }
                pprofile_release(profile);
            }
        }
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char proxiable[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_PROXIABLE,
                       proxiable, sizeof(proxiable)))
        {
            proxiable[sizeof(proxiable) - 1] = 0;
            return atoi(proxiable);
        }
    }
    return 0;
}

static
BOOL
get_default_publicip_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_PUBLICIP,
                                   result);
}

DWORD
Leash_reset_default_publicip(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_PUBLICIP);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_publicip(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_PUBLICIP, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_publicip(
    )
{
    HMODULE hmLeash;
    char env[32];
    DWORD result;

    if(GetEnvironmentVariable("PUBLICIP",env,sizeof(env)))
    {
        return atoi(env);
    }

    if (get_default_publicip_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_publicip_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char publicip[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_PUBLICIP,
                       publicip, sizeof(publicip)))
        {
            publicip[sizeof(publicip) - 1] = 0;
            return atoi(publicip);
        }
    }
    return 0;
}

static
BOOL
get_default_use_krb4_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_USEKRB4,
                                   result);
}

DWORD
Leash_reset_default_use_krb4(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_USEKRB4);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_use_krb4(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_USEKRB4, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_use_krb4(
    )
{
    HMODULE hmLeash;
    char env[32];
    DWORD result;

    if(GetEnvironmentVariable("USEKRB4",env,sizeof(env)))
    {
        return atoi(env);
    }

    if (get_default_use_krb4_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_use_krb4_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char use_krb4[80];
        if (LoadString(hmLeash, LSH_DEFAULT_TICKET_USEKRB4,
                       use_krb4, sizeof(use_krb4)))
        {
            use_krb4[sizeof(use_krb4) - 1] = 0;
            return atoi(use_krb4);
        }
    }
    return 1;	/* use krb4 unless otherwise specified */
}

static
BOOL
get_hide_kinit_options_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_KINIT_OPT,
                                   result);
}

DWORD
Leash_reset_hide_kinit_options(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_KINIT_OPT);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_hide_kinit_options(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_KINIT_OPT, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_hide_kinit_options(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_hide_kinit_options_from_registry(HKEY_CURRENT_USER, &result) ||
        get_hide_kinit_options_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char use_krb4[80];
        if (LoadString(hmLeash, LSH_DEFAULT_DIALOG_KINIT_OPT,
                       use_krb4, sizeof(use_krb4)))
        {
            use_krb4[sizeof(use_krb4) - 1] = 0;
            return atoi(use_krb4);
        }
    }
    return 0;	/* hide unless otherwise indicated */
}



static
BOOL
get_default_life_min_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_LIFE_MIN,
                                   result);
}

DWORD
Leash_reset_default_life_min(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_LIFE_MIN);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_life_min(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_LIFE_MIN, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_life_min(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_default_life_min_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_life_min_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char use_krb4[80];
        if (LoadString(hmLeash, LSH_DEFAULT_DIALOG_LIFE_MIN,
                       use_krb4, sizeof(use_krb4)))
        {
            use_krb4[sizeof(use_krb4) - 1] = 0;
            return atoi(use_krb4);
        }
    }
    return 5; 	/* 5 minutes */
}

static
BOOL
get_default_life_max_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_LIFE_MAX,
                                   result);
}

DWORD
Leash_reset_default_life_max(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_LIFE_MAX);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_life_max(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_LIFE_MAX, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_life_max(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_default_life_max_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_life_max_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char use_krb4[80];
        if (LoadString(hmLeash, LSH_DEFAULT_DIALOG_LIFE_MAX,
                       use_krb4, sizeof(use_krb4)))
        {
            use_krb4[sizeof(use_krb4) - 1] = 0;
            return atoi(use_krb4);
        }
    }
    return 1440;
}

static
BOOL
get_default_renew_min_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_RENEW_MIN,
                                   result);
}

DWORD
Leash_reset_default_renew_min(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_RENEW_MIN);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_renew_min(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_RENEW_MIN, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_renew_min(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_default_renew_min_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_renew_min_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char use_krb4[80];
        if (LoadString(hmLeash, LSH_DEFAULT_DIALOG_RENEW_MIN,
                       use_krb4, sizeof(use_krb4)))
        {
            use_krb4[sizeof(use_krb4) - 1] = 0;
            return atoi(use_krb4);
        }
    }
    return 600;  	/* 10 hours */
}

static
BOOL
get_default_renew_max_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_RENEW_MAX,
                                   result);
}

DWORD
Leash_reset_default_renew_max(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_RENEW_MAX);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_renew_max(
    DWORD minutes
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_RENEW_MAX, 0, REG_DWORD,
                       (LPBYTE) &minutes, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_renew_max(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_default_renew_max_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_renew_max_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char use_krb4[80];
        if (LoadString(hmLeash, LSH_DEFAULT_DIALOG_RENEW_MAX,
                       use_krb4, sizeof(use_krb4)))
        {
            use_krb4[sizeof(use_krb4) - 1] = 0;
            return atoi(use_krb4);
        }
    }
    return 60 * 24 * 30;
}

static
BOOL
get_lock_file_locations_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_LOCK_LOCATION,
                                   result);
}

DWORD
Leash_reset_lock_file_locations(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_LOCK_LOCATION);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_lock_file_locations(
    DWORD onoff
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_LOCK_LOCATION, 0, REG_DWORD,
                       (LPBYTE) &onoff, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_lock_file_locations(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_lock_file_locations_from_registry(HKEY_CURRENT_USER, &result) ||
        get_lock_file_locations_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char lock_file_locations[80];
        if (LoadString(hmLeash, LSH_DEFAULT_DIALOG_LOCK_LOCATION,
                       lock_file_locations, sizeof(lock_file_locations)))
        {
            lock_file_locations[sizeof(lock_file_locations) - 1] = 0;
            return atoi(lock_file_locations);
        }
    }
    return 0;
}

static
BOOL
get_default_uppercaserealm_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_SETTINGS_REGISTRY_KEY_NAME,
                                   LEASH_SETTINGS_REGISTRY_VALUE_UPPERCASEREALM,
                                   result);
}

DWORD
Leash_reset_default_uppercaserealm(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_SETTINGS_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_SETTINGS_REGISTRY_VALUE_UPPERCASEREALM);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_uppercaserealm(
    DWORD onoff
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_SETTINGS_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_SETTINGS_REGISTRY_VALUE_UPPERCASEREALM, 0, REG_DWORD,
                       (LPBYTE) &onoff, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_uppercaserealm(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_default_uppercaserealm_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_uppercaserealm_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char uppercaserealm[80];
        if (LoadString(hmLeash, LSH_DEFAULT_UPPERCASEREALM,
                       uppercaserealm, sizeof(uppercaserealm)))
        {
            uppercaserealm[sizeof(uppercaserealm) - 1] = 0;
            return atoi(uppercaserealm);
        }
    }
    return 1;
}

static
BOOL
get_default_mslsa_import_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_SETTINGS_REGISTRY_KEY_NAME,
                                   LEASH_SETTINGS_REGISTRY_VALUE_MSLSA_IMPORT,
                                   result);
}

DWORD
Leash_reset_default_mslsa_import(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_SETTINGS_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_SETTINGS_REGISTRY_VALUE_MSLSA_IMPORT);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_mslsa_import(
    DWORD onoffmatch
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_SETTINGS_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_SETTINGS_REGISTRY_VALUE_MSLSA_IMPORT, 0, REG_DWORD,
                       (LPBYTE) &onoffmatch, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_mslsa_import(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_default_mslsa_import_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_mslsa_import_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char mslsa_import[80];
        if (LoadString(hmLeash, LSH_DEFAULT_MSLSA_IMPORT,
                       mslsa_import, sizeof(mslsa_import)))
        {
            mslsa_import[sizeof(mslsa_import) - 1] = 0;
            return atoi(mslsa_import);
        }
    }
    return 2;   /* import only when mslsa realm matches default */
}


static
BOOL
get_default_preserve_kinit_settings_from_registry(
    HKEY hBaseKey,
    DWORD * result
    )
{
    return get_DWORD_from_registry(hBaseKey,
                                   LEASH_REGISTRY_KEY_NAME,
                                   LEASH_REGISTRY_VALUE_PRESERVE_KINIT,
                                   result);
}

DWORD
Leash_reset_default_preserve_kinit_settings(
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0, KEY_WRITE, &hKey);
    if (rc)
        return rc;

    rc = RegDeleteValue(hKey, LEASH_REGISTRY_VALUE_PRESERVE_KINIT);
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_set_default_preserve_kinit_settings(
    DWORD onoff
    )
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, LEASH_REGISTRY_KEY_NAME, 0,
                        0, 0, KEY_WRITE, 0, &hKey, 0);
    if (rc)
        return rc;

    rc = RegSetValueEx(hKey, LEASH_REGISTRY_VALUE_PRESERVE_KINIT, 0, REG_DWORD,
                       (LPBYTE) &onoff, sizeof(DWORD));
    RegCloseKey(hKey);

    return rc;
}

DWORD
Leash_get_default_preserve_kinit_settings(
    )
{
    HMODULE hmLeash;
    DWORD result;

    if (get_default_preserve_kinit_settings_from_registry(HKEY_CURRENT_USER, &result) ||
        get_default_preserve_kinit_settings_from_registry(HKEY_LOCAL_MACHINE, &result))
    {
        return result;
    }

    hmLeash = GetModuleHandle(LEASH_DLL);
    if (hmLeash)
    {
        char preserve_kinit_settings[80];
        if (LoadString(hmLeash, LSH_DEFAULT_PRESERVE_KINIT,
                       preserve_kinit_settings, sizeof(preserve_kinit_settings)))
        {
            preserve_kinit_settings[sizeof(preserve_kinit_settings) - 1] = 0;
            return atoi(preserve_kinit_settings);
        }
    }
    return 1;
}

void
Leash_reset_defaults(void)
{
    Leash_reset_default_lifetime();
    Leash_reset_default_renew_till();
    Leash_reset_default_renewable();
    Leash_reset_default_forwardable();
    Leash_reset_default_noaddresses();
    Leash_reset_default_proxiable();
    Leash_reset_default_publicip();
    Leash_reset_default_use_krb4();
    Leash_reset_hide_kinit_options();
    Leash_reset_default_life_min();
    Leash_reset_default_life_max();
    Leash_reset_default_renew_min();
    Leash_reset_default_renew_max();
    Leash_reset_default_uppercaserealm();
    Leash_reset_default_mslsa_import();
    Leash_reset_default_preserve_kinit_settings();
}

static BOOL CALLBACK
EnumChildProc(HWND hwnd, LPARAM lParam)
{
    HWND * h = (HWND *)lParam;
    *h = hwnd;
    return FALSE;
}


static HWND
FindFirstChildWindow(HWND parent)
{
    HWND hFirstChild = 0;
    EnumChildWindows(parent, EnumChildProc, (LPARAM) &hFirstChild);
    return hFirstChild;
}

static int
acquire_tkt_send_msg(krb5_context ctx, const char * title,
		     const char * ccachename,
		     krb5_principal desiredKrb5Principal,
		     const char * out_ccname, int out_cclen)
{
    krb5_error_code 	err;
    HWND    	        hNetIdMgr;
    HWND    		hForeground;
    char		*desiredName = 0;
    char                *desiredRealm = 0;

    /* do we want a specific client principal? */
    if (desiredKrb5Principal != NULL) {
	err = pkrb5_unparse_name (ctx, desiredKrb5Principal, &desiredName);
	if (!err) {
	    char * p;
	    for (p = desiredName; *p && *p != '@'; p++);
	    if ( *p == '@' ) {
		*p = '\0';
		desiredRealm = ++p;
	    }
	}
    }

    hForeground = GetForegroundWindow();
    hNetIdMgr = FindWindow("IDMgrRequestDaemonCls", "IDMgrRequestDaemon");
    if (hNetIdMgr != NULL) {
	HANDLE hMap;
	DWORD  tid = GetCurrentThreadId();
	char mapname[256];
	NETID_DLGINFO *dlginfo;

	sprintf(mapname,"Local\\NetIDMgr_DlgInfo_%lu",tid);

	hMap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
				 0, 4096, mapname);
	if (hMap == NULL) {
	    return -1;
	} else if (hMap != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
	    CloseHandle(hMap);
	    return -1;
	}

	dlginfo = (NETID_DLGINFO *)MapViewOfFileEx(hMap, FILE_MAP_READ|FILE_MAP_WRITE,
						 0, 0, 4096, NULL);
	if (dlginfo == NULL) {
	    CloseHandle(hMap);
	    return -1;
	}

	memset(dlginfo, 0, sizeof(NETID_DLGINFO));

	dlginfo->size = sizeof(NETID_DLGINFO);
	dlginfo->dlgtype = NETID_DLGTYPE_TGT;
	dlginfo->in.use_defaults = 1;

	if (title) {
	    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED|MB_ERR_INVALID_CHARS,
				title, -1,
				dlginfo->in.title, NETID_TITLE_SZ);
	} else if (desiredName && (strlen(desiredName) + strlen(desiredRealm) + 32 < NETID_TITLE_SZ)) {
	    char mytitle[NETID_TITLE_SZ];
	    sprintf(mytitle, "Obtain Kerberos TGT for %s@%s",desiredName,desiredRealm);
	    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED|MB_ERR_INVALID_CHARS,
				mytitle, -1,
				dlginfo->in.title, NETID_TITLE_SZ);
	} else {
	    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED|MB_ERR_INVALID_CHARS,
				"Obtain Kerberos TGT", -1,
				dlginfo->in.title, NETID_TITLE_SZ);
	}
	if (desiredName)
	    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED|MB_ERR_INVALID_CHARS,
				desiredName, -1,
				dlginfo->in.username, NETID_USERNAME_SZ);
	if (desiredRealm)
	    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED|MB_ERR_INVALID_CHARS,
				desiredRealm, -1,
				dlginfo->in.realm, NETID_REALM_SZ);
	if (ccachename)
	    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED|MB_ERR_INVALID_CHARS,
				ccachename, -1,
				dlginfo->in.ccache, NETID_CCACHE_NAME_SZ);
	SendMessage(hNetIdMgr, 32810, 0, (LPARAM) tid);

	if (out_ccname && out_cclen > 0) {
	    WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, dlginfo->out.ccache, -1,
				out_ccname, out_cclen, NULL, NULL);
	}

	UnmapViewOfFile(dlginfo);
	CloseHandle(hMap);
    } else {
	HGLOBAL 		hData;
	HWND hLeash = FindWindow("LEASH.0WNDCLASS", NULL);
	hLeash = FindFirstChildWindow(hLeash);

	/* construct a marshalling of data
	 *   <title><principal><realm><ccache>
	 * then send to Leash
	 */

	hData = GlobalAlloc( GHND, 4096 );
	SetForegroundWindow(hLeash);
	if ( hData && hLeash ) {
	    char * strs = GlobalLock(hData);
	    if ( strs ) {
		if (title)
		    strcpy(strs, title);
		else if (desiredName)
		    sprintf(strs, "Obtain Kerberos TGT for %s@%s",desiredName,desiredRealm);
		else
		    strcpy(strs, "Obtain Kerberos TGT");
		strs += strlen(strs) + 1;
		if ( desiredName ) {
		    strcpy(strs, desiredName);
		    strs += strlen(strs) + 1;
		    if (desiredRealm) {
			strcpy(strs, desiredRealm);
			strs += strlen(strs) + 1;
		    }
		} else {
		    *strs = 0;
		    strs++;
		    *strs = 0;
		    strs++;
		}

		/* Append the ccache name */
		if (ccachename)
		    strcpy(strs, ccachename);
		else
		    *strs = 0;
		strs++;

		GlobalUnlock( hData );
		SendMessage(hLeash, 32810, 0, (LPARAM) hData);
	    }
	}
	GlobalFree( hData );
    }

    SetForegroundWindow(hForeground);
    if (desiredName != NULL)
	pkrb5_free_unparsed_name(ctx, desiredName);

    return 0;
}

static void
acquire_tkt_no_princ(krb5_context context, char * ccname, int cclen)
{
    TicketList 		*list = NULL;
    TICKETINFO   	ticketinfo;
    krb5_context        ctx;
    DWORD 		dwMsLsaImport = Leash_get_default_mslsa_import();
    DWORD		gle;
    char ccachename[272]="";
    char loginenv[16];
    BOOL prompt;

    GetEnvironmentVariable("KERBEROSLOGIN_NEVER_PROMPT", loginenv, sizeof(loginenv));
    prompt = (GetLastError() == ERROR_ENVVAR_NOT_FOUND);

    ctx = context;

    SetLastError(0);
    GetEnvironmentVariable("KRB5CCNAME", ccachename, sizeof(ccachename));
    gle = GetLastError();
    if ( ((gle == ERROR_ENVVAR_NOT_FOUND) || !ccachename[0]) && context ) {
        char * ccdef = pkrb5_cc_default_name(ctx);
	SetEnvironmentVariable("KRB5CCNAME", ccdef ? ccdef : NULL);
	GetEnvironmentVariable("KRB5CCNAME", ccachename, sizeof(ccachename));
    }

    not_an_API_LeashKRB5GetTickets(&ticketinfo,&list,&ctx);
    not_an_API_LeashFreeTicketList(&list);

    if ( ticketinfo.btickets != GOOD_TICKETS &&
         dwMsLsaImport && Leash_importable() ) {
        // We have the option of importing tickets from the MSLSA
        // but should we?  Do the tickets in the MSLSA cache belong
        // to the default realm used by Leash?  Does the default
	// ccache name specify a principal name?  Only import if we
	// aren't going to break the default identity as specified
	// by the user in Network Identity Manager.
        int import = 0;
	BOOL isCCPrinc;

	/* Determine if the default ccachename is principal name.  If so, don't
	* import the MSLSA: credentials into it unless the names match.
	*/
	isCCPrinc = (strncmp("API:",ccachename, 4) == 0 && strchr(ccachename, '@'));

        if ( dwMsLsaImport == 1 && !isCCPrinc ) { /* always import */
            import = 1;
        } else if ( dwMsLsaImport ) {      	  /* import when realms match */
            krb5_error_code code;
            krb5_ccache mslsa_ccache=NULL;
            krb5_principal princ = NULL;
	    char *mslsa_principal = NULL;
            char ms_realm[128] = "", *def_realm = NULL, *r;
            int i;

            if (code = pkrb5_cc_resolve(ctx, "MSLSA:", &mslsa_ccache))
                goto cleanup;

            if (code = pkrb5_cc_get_principal(ctx, mslsa_ccache, &princ))
                goto cleanup;

            for ( r=ms_realm, i=0; i<krb5_princ_realm(ctx, princ)->length; r++, i++ ) {
                *r = krb5_princ_realm(ctx, princ)->data[i];
            }
            *r = '\0';

            if (code = pkrb5_get_default_realm(ctx, &def_realm))
                goto cleanup;

	    if (code = pkrb5_unparse_name(ctx, princ, &mslsa_principal))
		goto cleanup;

            import = (!isCCPrinc && !strcmp(def_realm, ms_realm)) ||
		(isCCPrinc && !strcmp(&ccachename[4], mslsa_principal));

          cleanup:
	    if (mslsa_principal)
		pkrb5_free_unparsed_name(ctx, mslsa_principal);

            if (def_realm)
                pkrb5_free_default_realm(ctx, def_realm);

            if (princ)
                pkrb5_free_principal(ctx, princ);

            if (mslsa_ccache)
                pkrb5_cc_close(ctx, mslsa_ccache);
        }

        if ( import ) {
            Leash_import();

            not_an_API_LeashKRB5GetTickets(&ticketinfo,&list,&ctx);
            not_an_API_LeashFreeTicketList(&list);
        }
    }

    if ( prompt && ticketinfo.btickets != GOOD_TICKETS ) {
	acquire_tkt_send_msg(ctx, NULL, ccachename, NULL, ccname, cclen);
        /*
         * If the ticket manager returned an alternative credential cache
         * remember it as the default for this process.
         */
        if ( ccname && ccname[0] && strcmp(ccachename,ccname) ) {
            SetEnvironmentVariable("KRB5CCNAME",ccname);
        }

    } else if (ccachename[0] && ccname) {
	strncpy(ccname, ccachename, cclen);
	ccname[cclen-1] = '\0';
    }

    if ( !context )
        pkrb5_free_context(ctx);
}


static void
acquire_tkt_for_princ(krb5_context context, krb5_principal desiredPrincipal,
		      char * ccname, int cclen)
{
    TicketList 		*list = NULL;
    TICKETINFO   	ticketinfo;
    krb5_context        ctx;
    DWORD 		dwMsLsaImport = Leash_get_default_mslsa_import();
    DWORD		gle;
    char 		ccachename[272]="";
    char 		loginenv[16];
    BOOL 		prompt;
    char 		*name = NULL;

    GetEnvironmentVariable("KERBEROSLOGIN_NEVER_PROMPT", loginenv, sizeof(loginenv));
    prompt = (GetLastError() == ERROR_ENVVAR_NOT_FOUND);

    ctx = context;

    SetLastError(0);
    GetEnvironmentVariable("KRB5CCNAME", ccachename, sizeof(ccachename));
    gle = GetLastError();
    if ( ((gle == ERROR_ENVVAR_NOT_FOUND) || !ccachename[0]) && context ) {
        char * ccdef = pkrb5_cc_default_name(ctx);
	SetEnvironmentVariable("KRB5CCNAME", ccdef ? ccdef : NULL);
	GetEnvironmentVariable("KRB5CCNAME", ccachename, sizeof(ccachename));
    }

    not_an_API_LeashKRB5GetTickets(&ticketinfo,&list,&ctx);
    not_an_API_LeashFreeTicketList(&list);

    pkrb5_unparse_name(ctx, desiredPrincipal, &name);

    if ( ticketinfo.btickets != GOOD_TICKETS &&
         dwMsLsaImport && Leash_importable() ) {
        // We have the option of importing tickets from the MSLSA
        // but should we?  Does the MSLSA principal match the requested
	// principal?  If not, there is no benefit to importing.
        int import = 0;
	krb5_error_code code;
	krb5_ccache mslsa_ccache=NULL;
	krb5_principal princ = NULL;

	if (code = pkrb5_cc_resolve(ctx, "MSLSA:", &mslsa_ccache))
	    goto cleanup;

	if (code = pkrb5_cc_get_principal(ctx, mslsa_ccache, &princ))
	    goto cleanup;

	import = pkrb5_principal_compare(ctx, desiredPrincipal, princ);

      cleanup:
	if (princ)
	    pkrb5_free_principal(ctx, princ);

	if (mslsa_ccache)
	    pkrb5_cc_close(ctx, mslsa_ccache);


        if ( import ) {
	    /* Construct a new default ccache name into which the MSLSA:
	     * credentials can be imported, set the default ccache to that
	     * ccache, and then only import if that ccache does not already
	     * contain valid tickets */
	    sprintf(ccachename, "API:%s", name);

	    SetEnvironmentVariable("KRB5CCNAME", ccachename);

            not_an_API_LeashKRB5GetTickets(&ticketinfo,&list,&ctx);
            not_an_API_LeashFreeTicketList(&list);

	    if (ticketinfo.btickets != GOOD_TICKETS) {
		Leash_import();

		not_an_API_LeashKRB5GetTickets(&ticketinfo,&list,&ctx);
		not_an_API_LeashFreeTicketList(&list);
	    }
	}
    }

    if (prompt) {
	if (ticketinfo.btickets != GOOD_TICKETS || strcmp(name,ticketinfo.principal)) {
	    acquire_tkt_send_msg(ctx, NULL, ccachename, desiredPrincipal, ccname, cclen);
            /*
             * If the ticket manager returned an alternative credential cache
             * remember it as the default for this process.
             */
            if ( ccname && ccname[0] && strcmp(ccachename,ccname) ) {
                SetEnvironmentVariable("KRB5CCNAME",ccname);
            }
	} else if (ccachename[0] && ccname) {
	    strncpy(ccname, ccachename, cclen);
	    ccname[cclen-1] = '\0';
	}
    }

    if (name)
	pkrb5_free_unparsed_name(ctx, name);

    if ( !context )
        pkrb5_free_context(ctx);
}


static int
leash_int_get_princ_expiration_time(krb5_context ctx, krb5_ccache cc,
				    krb5_principal desiredPrincipal,
				    krb5_timestamp * pexpiration)
{
    krb5_principal principal = 0;
    char * princ_name = NULL;
    char * desired_name = NULL;
    krb5_creds creds;
    krb5_error_code code, code2;
    krb5_error_code cc_code;
    krb5_cc_cursor cur;
    krb5_timestamp now, expiration = 0;

    int rv = -1;

    if (!ctx || !cc || !desiredPrincipal || !pexpiration)
        return -1;

    code = pkrb5_cc_get_principal(ctx, cc, &principal);
    if ( code )
        return -1;


    code = pkrb5_unparse_name(ctx, desiredPrincipal, &desired_name);
    code2 = pkrb5_unparse_name(ctx, principal, &princ_name);

    /* compare principal to ident. */
    if ( code || !princ_name ||	code2 || !desired_name ||
	 strcmp(princ_name, desired_name) ) {
        if (princ_name)
            pkrb5_free_unparsed_name(ctx, princ_name);
        if (desired_name)
            pkrb5_free_unparsed_name(ctx, desired_name);
        pkrb5_free_principal(ctx, principal);
        return -1;
    }

    pkrb5_free_unparsed_name(ctx, princ_name);
    pkrb5_free_unparsed_name(ctx, desired_name);
    pkrb5_free_principal(ctx, principal);

    code = pkrb5_timeofday(ctx, &now);

    if (code)
        return -1;

    cc_code = pkrb5_cc_start_seq_get(ctx, cc, &cur);

    while (!(cc_code = pkrb5_cc_next_cred(ctx, cc, &cur, &creds))) {
        krb5_data * c0 = krb5_princ_name(ctx, creds.server);
        krb5_data * c1  = krb5_princ_component(ctx, creds.server, 1);
        krb5_data * r = krb5_princ_realm(ctx, creds.server);

        if ( c0 && c1 && r && c1->length == r->length &&
             !strncmp(c1->data,r->data,r->length) &&
             !strncmp("krbtgt",c0->data,c0->length) ) {

            /* we have a TGT, check for the expiration time.
             * if it is valid and renewable, use the renew time
             */

            if (!(creds.ticket_flags & TKT_FLG_INVALID) &&
                creds.times.starttime < now && creds.times.endtime > now) {
                expiration = creds.times.endtime;

                if ((creds.ticket_flags & TKT_FLG_RENEWABLE) &&
                    (creds.times.renew_till > creds.times.endtime)) {
                    expiration = creds.times.renew_till;
                }
            }
        }
    }

    if (cc_code == KRB5_CC_END) {
        cc_code = pkrb5_cc_end_seq_get(ctx, cc, &cur);
        rv = 0;
        *pexpiration = expiration;
    }

    return rv;
}

/* returns 0 on success */
static int
leash_int_find_ccache_for_princ(krb5_context ctx, krb5_principal princ,
				char * buffer, int * pcbbuf)
{
    krb5_ccache         cache = 0;
    krb5_error_code     code;
    apiCB *             cc_ctx = 0;
    struct _infoNC **   pNCi = NULL;
    int                 i;
    krb5_timestamp      expiration = 0;
    krb5_timestamp      best_match_expiration = 0;
    char                best_match_ccname[256] = "";
    DWORD 		dwMsLsaImport = Leash_get_default_mslsa_import();

    if (!buffer || !pcbbuf)
	return -1;

    code = cc_initialize(&cc_ctx, CC_API_VER_2, NULL, NULL);
    if (code)
        goto _exit;

    code = cc_get_NC_info(cc_ctx, &pNCi);
    if (code)
        goto _exit;

    for(i=0; pNCi[i]; i++) {
        if (pNCi[i]->vers != CC_CRED_V5)
            continue;

        code = pkrb5_cc_resolve(ctx, pNCi[i]->name, &cache);
        if (code)
            continue;

        /* need a function to check the cache for the identity
         * and determine if it has valid tickets.  If it has
         * the right identity and valid tickets, store the
         * expiration time and the cache name.  If it has the
         * right identity but no valid tickets, store the ccache
         * name and an expiration time of zero.  if it does not
         * have the right identity don't save the name.
         *
         * Keep searching to find the best cache available.
         */

        if (!leash_int_get_princ_expiration_time(ctx, cache, princ,
						 &expiration)) {
            if ( expiration > best_match_expiration ) {
                best_match_expiration = expiration;
                strncpy(best_match_ccname, "API:",
                              sizeof(best_match_ccname));
                strncat(best_match_ccname, pNCi[i]->name,
			 sizeof(best_match_ccname));
		best_match_ccname[sizeof(best_match_ccname)-1] = '\0';
                expiration = 0;
            }
        }

        if(ctx != NULL && cache != NULL)
            pkrb5_cc_close(ctx, cache);
        cache = 0;
    }

    if (dwMsLsaImport) {
	code = pkrb5_cc_resolve(ctx, "MSLSA:", &cache);
	if (code == 0 && cache) {
	    if (!leash_int_get_princ_expiration_time(ctx, cache, princ,
						      &expiration)) {
		if ( expiration > best_match_expiration ) {
		    best_match_expiration = expiration;
		    strcpy(best_match_ccname, "MSLSA:");
		    expiration = 0;
		}
	    }
	}
    }

    if (ctx != NULL && cache != NULL)
	pkrb5_cc_close(ctx, cache);

    cache = 0;

 _exit:
    if (pNCi)
        cc_free_NC_info(cc_ctx, &pNCi);

    if (cc_ctx)
        cc_shutdown(&cc_ctx);

    if (best_match_ccname[0]) {
	strncpy(buffer, best_match_ccname, *pcbbuf);
	buffer[*pcbbuf-1]='\0';

	*pcbbuf = strlen(buffer) + 1;
	return 0;
    }

    return -1;
}

void FAR
not_an_API_Leash_AcquireInitialTicketsIfNeeded(krb5_context context,
					       krb5_principal desiredKrb5Principal,
					       char * ccname, int cclen)
{
    char		*desiredName = 0;
    char                *desiredRealm = 0;
    TicketList 		*list = NULL;
    char ccachename[272]="";

    if (!desiredKrb5Principal) {
	acquire_tkt_no_princ(context, ccname, cclen);
    } else {
	if (leash_int_find_ccache_for_princ(context, desiredKrb5Principal, ccname, &cclen))
	    acquire_tkt_for_princ(context, desiredKrb5Principal, ccname, cclen);
    }
    return;
}
