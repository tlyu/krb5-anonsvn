krb5_error_code krb5_init_context(krb5_context *);
void krb5_free_context(krb5_context);
krb5_error_code krb5_get_credentials(krb5_context, const krb5_flags, krb5_ccache, krb5_creds *, krb5_creds **);
krb5_error_code krb5_mk_req_extended(krb5_context, krb5_auth_context *, const krb5_flags, krb5_data *, krb5_creds *, krb5_data * );
krb5_error_code krb5_rd_rep(krb5_context, krb5_auth_context, const krb5_data *, krb5_ap_rep_enc_part **);
krb5_error_code krb5_copy_keyblock(krb5_context, const krb5_keyblock *, krb5_keyblock **);
void krb5_init_ets(krb5_context);
krb5_error_code krb5_cc_default(krb5_context, krb5_ccache *);
void krb5_free_principal(krb5_context, krb5_principal );
void krb5_free_creds(krb5_context, krb5_creds *);
void krb5_free_cred_contents(krb5_context, krb5_creds *);
void krb5_free_keyblock(krb5_context, krb5_keyblock *);
void krb5_free_ap_rep_enc_part(krb5_context, krb5_ap_rep_enc_part *);
krb5_error_code krb5_sname_to_principal(krb5_context, const char *, const char *, krb5_int32, krb5_principal *);
krb5_error_code krb5_fwd_tgt_creds(krb5_context, krb5_auth_context, char *, krb5_principal, krb5_principal, krb5_ccache, int forwardable, krb5_data *);
krb5_error_code krb5_auth_con_init(krb5_context, krb5_auth_context *);
krb5_error_code krb5_auth_con_free(krb5_context, krb5_auth_context);
krb5_error_code krb5_auth_con_setflags(krb5_context, krb5_auth_context, krb5_int32);
krb5_error_code krb5_auth_con_setaddrs(krb5_context, krb5_auth_context, krb5_address *, krb5_address *);
krb5_error_code krb5_auth_con_setports(krb5_context, krb5_auth_context, krb5_address *, krb5_address *);
krb5_error_code krb5_auth_con_getlocalsubkey(krb5_context, krb5_auth_context, krb5_keyblock **);
krb5_error_code krb5_auth_con_genaddrs(krb5_context, krb5_auth_context, int, int);
int mit_des_ecb_encrypt(const mit_des_cblock  *, mit_des_cblock  *, mit_des_key_schedule , int );
krb5_error_code mit_des_init_random_key( const krb5_encrypt_block  *, const krb5_keyblock  *, krb5_pointer  *);
int mit_des_key_sched(mit_des_cblock , mit_des_key_schedule );
krb5_error_code mit_des_random_key( const krb5_encrypt_block  *, krb5_pointer , krb5_keyblock  *  *);
void com_err_va(const char  *whoami, errcode_t code, const char  *fmt, va_list ap));
