//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//
//  Copyright (C) Microsoft Corporation, 1997 - 1999
//
//  File:       resource.h
//
//--------------------------------------------------------------------------


#define         IDS_ROOT_MSG_BOX_TITLE                      6100
#define         IDS_ROOT_MSG_BOX_SUBJECT                    6110
#define         IDS_ROOT_MSG_BOX_ISSUER                     6120
#define         IDS_ROOT_MSG_BOX_SELF_ISSUED                6121
#define         IDS_ROOT_MSG_BOX_SERIAL_NUMBER              6130
#define         IDS_ROOT_MSG_BOX_SHA1_THUMBPRINT            6140
#define         IDS_ROOT_MSG_BOX_MD5_THUMBPRINT             6150
#define         IDS_ROOT_MSG_BOX_TIME_VALIDITY              6160
#define         IDS_ROOT_MSG_BOX_ADD_ACTION                 6170
#define         IDS_ROOT_MSG_BOX_DELETE_ACTION              6180
#define         IDS_ROOT_MSG_BOX_DELETE_UNKNOWN_PROT_ROOTS  6190

#define         IDS_INSTALLCA                               6200
#define         IDS_TOO_MANY_CA_CERTS                       6210

#define         IDS_ADD_ROOT_MSG_BOX_TITLE                  6250
#define         IDS_ADD_ROOT_MSG_BOX_INTRO                  6251
#define         IDS_ADD_ROOT_MSG_BOX_BODY_0                 6255
#define         IDS_ADD_ROOT_MSG_BOX_BODY_1                 6256
#define         IDS_ADD_ROOT_MSG_BOX_END_0                  6260
#define         IDS_ADD_ROOT_MSG_BOX_END_1                  6261

// Following resources are used to format chain extended error information
#define         IDS_INVALID_ISSUER_NAME_CONSTRAINT_EXT      6500
#define         IDS_INVALID_SUBJECT_NAME_CONSTRAINT_INFO    6501
#define         IDS_NOT_SUPPORTED_ENTRY_NAME_CONSTRAINT     6502
#define         IDS_NOT_SUPPORTED_PERMITTED_NAME_CONSTRAINT 6503
#define         IDS_NOT_SUPPORTED_EXCLUDED_NAME_CONSTRAINT  6504
#define         IDS_NOT_PERMITTED_ENTRY_NAME_CONSTRAINT     6505
#define         IDS_EXCLUDED_ENTRY_NAME_CONSTRAINT          6506
#define         IDS_NOT_DEFINED_ENTRY_NAME_CONSTRAINT       6507


#define         IDS_BASIC_CONS2_PATH                    7002
#define         IDS_BASIC_CONS2_NONE                    7003
#define         IDS_NONE                                7004
#define         IDS_SUB_EE                              7005
#define         IDS_SUB_CA                              7006
#define         IDS_UNSPECIFIED                         7007
#define         IDS_KEY_COMPROMISE                      7008
#define         IDS_CA_COMPROMISE                       7009
#define         IDS_AFFILIATION_CHANGED                 7010
#define         IDS_SUPERSEDED                          7011
#define         IDS_CESSATION_OF_OPERATION              7012
#define         IDS_CERTIFICATE_HOLD                    7013
#define         IDS_UNKNOWN_VALUE                       7014
#define         IDS_REMOVE_FROM_CRL                     7015
#define         IDS_SUBTREE_CONSTRAINT                  7016
#define         IDS_NO_INFO                             7017
#define         IDS_OTHER_NAME                          7018
#define         IDS_RFC822_NAME                         7019
#define         IDS_DNS_NAME                            7020
#define         IDS_X400_ADDRESS                        7021
#define         IDS_DIRECTORY_NAME                      7022
#define         IDS_EDI_PARTY_NAME                      7023
#define         IDS_URL                                 7024
#define         IDS_IP_ADDRESS                          7025
#define         IDS_REGISTERED_ID                       7026
#define         IDS_ALT_NAME_ENTRY_UNKNOWN              7027
#define         IDS_ALT_NAME_ENTRY                      7030
#define         IDS_YES                                 7031
#define         IDS_NO                                  7032
#define         IDS_AVAILABLE                           7033
#define         IDS_NOT_AVAILABLE                       7034
#define         IDS_SPC_FINANCIAL_NOT_AVAIL             7035
#define         IDS_MIME_CAPABILITY                     7036
#define         IDS_SPC_FINANCIAL_AVAIL                 7037
#define         IDS_AUTH_KEY_ID                         7038
#define         IDS_AUTH_CERT_NUMBER                    7039
#define         IDS_AUTH_CERT_ISSUER                    7040
#define         IDS_DIG_SIG                             7041
#define         IDS_KEY_ENCIPHERMENT                    7042
#define         IDS_DATA_ENCIPHERMENT                   7043
#define         IDS_KEY_AGREEMENT                       7044
#define         IDS_CERT_SIGN                           7045
#define         IDS_OFFLINE_CRL_SIGN                    7046
#define         IDS_CRL_SIGN                            7047
#define         IDS_DECIPHER_ONLY                       7048
#define         IDS_NON_REPUDIATION                     7049
#define         IDS_ENCIPHER_ONLY                       7050
#define         IDS_MIME_CAPABILITY_NO_PARAM            7052
#define         IDS_ENHANCED_KEY_USAGE                  7053
#define         IDS_NO_ALT_NAME                         7054
#define         IDS_UNKNOWN_ACCESS_METHOD               7055
#define         IDS_AUTHORITY_ACCESS_INFO               7056
#define         IDS_CRL_REASON                          7057
#define         IDS_UNKNOWN_KEY_USAGE                   7058
#define         IDS_BIT_BLOB                            7059
#define         IDS_SUNDAY                              7060
#define         IDS_MONDAY                              IDS_SUNDAY+1  
#define         IDS_TUESDAY                             IDS_MONDAY+1  
#define         IDS_WED                                 IDS_TUESDAY+1 
#define         IDS_THUR                                IDS_WED+1     
#define         IDS_FRI                                 IDS_THUR+1    
#define         IDS_SAT                                 IDS_FRI+1     
#define         IDS_JAN                                 7070
#define         IDS_FEB                                 IDS_JAN+1   
#define         IDS_MAR                                 IDS_FEB+1   
#define         IDS_APR                                 IDS_MAR+1   
#define         IDS_MAY                                 IDS_APR+1   
#define         IDS_JUNE                                IDS_MAY+1   
#define         IDS_JULY                                IDS_JUNE+1  
#define         IDS_AUG                                 IDS_JULY+1  
#define         IDS_SEP                                 IDS_AUG+1   
#define         IDS_OCT                                 IDS_SEP+1   
#define         IDS_NOV                                 IDS_OCT+1   
#define         IDS_DEC                                 IDS_NOV+1
#define         IDS_AM                                  7083
#define         IDS_PM                                  7084 
#define         IDS_FILE_TIME                           7085
#define         IDS_FILE_TIME_DWORD                     7086
#define         IDS_KEY_ATTR_ID                         7087
#define         IDS_KEY_ATTR_USAGE                      7088
#define         IDS_KEY_ATTR_AFTER                      7089
#define         IDS_KEY_ATTR_BEFORE                     7090
#define         IDS_KEY_RES_USAGE                       7091
#define         IDS_KEY_RES_ID                          7092
#define         IDS_CRL_DIST_FULL_NAME                  7093
#define         IDS_CRL_DIST_ISSUER_RDN                 7094
#define         IDS_DWORD                               7095
#define         IDS_UNKNOWN_CRL_REASON                  7096
#define         IDS_CRL_DIST_NAME                       7097
#define         IDS_CRL_DIST_REASON                     7098
#define         IDS_CRL_DIST_ENTRY                      7099
#define         IDS_CRL_DIST_ISSUER                     7100
#define         IDS_POLICY_QUALIFIER                    7101
#define         IDS_POLICY_QUALIFIER_NO_BLOB            7102
#define         IDS_POLICY_QUALIFIER_INFO               7103
#define         IDS_CERT_POLICY_NO_QUA                  7104
#define         IDS_CERT_POLICY                         7105
#define         IDS_SPC_URL_LINK                        7107
#define         IDS_SPC_MONIKER_LINK                    7108
#define         IDS_SPC_FILE_LINK                       7109
#define         IDS_SPC_LINK_UNKNOWN                    7110
#define         IDS_IMAGE_LINK                          7111
#define         IDS_IMAGE_BITMAP                        7112
#define         IDS_IMAGE_METAFILE                      7113
#define         IDS_IMAGE_ENHANCED_METAFILE             7114
#define         IDS_IMAGE_GIFFILE                       7115
#define         IDS_AGENCY_POLICY_INFO                  7116
#define         IDS_AGENCY_POLICY_DSPLY                 7117
#define         IDS_AGENCY_LOGO_LINK                    7118
#define         IDS_SPC_OBJECT_NO_BLOB                  7119
#define         IDS_AGENCY_LOGO_IMAGE                   7120
#define         IDS_BASIC_CONS2_PATH_MULTI              7121
#define         IDS_BASIC_CONS2_NONE_MULTI              7122
#define         IDS_SUBTREE_CONSTRAINT_MULTI            7123
#define         IDS_SPC_FINANCIAL_AVAIL_MULTI           7124
#define         IDS_SPC_FINANCIAL_NOT_AVAIL_MULTI       7125
#define         IDS_MIME_CAPABILITY_MULTI               7126
#define         IDS_MIME_CAPABILITY_NO_PARAM_MULTI      7127
#define         IDS_AUTHORITY_ACCESS_INFO_MULTI         7128
#define         IDS_AUTHORITY_ACCESS_NO_METHOD_MULTI    7129
#define         IDS_KEY_ATTR_ID_MULTI                   7130
#define         IDS_KEY_ATTR_USAGE_MULTI                7131
#define         IDS_KEY_ATTR_BEFORE_MULTI               7132
#define         IDS_KEY_ATTR_AFTER_MULTI                7133
#define         IDS_KEY_RES_ID_MULTI                    7134
#define         IDS_KEY_RES_USAGE_MULTI                 7135
#define         IDS_CRL_DIST_FULL_NAME_MULTI            7136
#define         IDS_CRL_DIST_NAME_MULTI                 7137
#define         IDS_CRL_DIST_REASON_MULTI               7138
#define         IDS_CRL_DIST_ISSUER_MULTI               7139
#define         IDS_CRL_DIST_ENTRY_MULTI                7140
#define         IDS_POLICY_QUALIFIER_MULTI              7141
#define         IDS_POLICY_QUALIFIER_INFO_MULTI         7142
#define         IDS_CERT_POLICY_MULTI                   7143
#define         IDS_CERT_POLICY_NO_QUA_MULTI            7144
#define         IDS_SPC_MONIKER_LINK_MULTI              7145
#define         IDS_IMAGE_LINK_MULTI                    7146
#define         IDS_IMAGE_BITMAP_MULTI                  7147
#define         IDS_IMAGE_METAFILE_MULTI                7148
#define         IDS_IMAGE_ENHANCED_METAFILE_MULTI       7149
#define         IDS_IMAGE_GIFFILE_MULTI                 7150
#define         IDS_AGENCY_POLICY_INFO_MULTI            7151
#define         IDS_AGENCY_POLICY_DSPLY_MULTI           7152
#define         IDS_AGENCY_LOGO_IMAGE_MULTI             7153
#define         IDS_AGENCY_LOGO_LINK_MULTI              7154
#define         IDS_AUTH_CERT_ISSUER_MULTI              7155
#define         IDS_AUTHORITY_ACCESS_NO_METHOD          7156
#define         IDS_SPC_OBJECT_DATA                     7157
#define         IDS_SPC_OBJECT_CLASS                    7158
#define         IDS_ONE_TAB                             7159
#define         IDS_TWO_TABS                            7160
#define         IDS_THREE_TABS                          7161
#define         IDS_FOUR_TABS                           7162
#define         IDS_FRMT_SPACE                          7170
#define         IDS_FRMT_A                              7171
#define         IDS_FRMT_ZERO                           7172
#define         IDS_FRMT_HEX                            7173
#define         IDS_DIRECTORY_NAME_MULTI                7174
#define         IDS_UNICODE_STRING                      7175
#define         IDS_UNICODE_STRING_MULTI                7176
#define         IDS_CA_VERSION                          7177
#define         IDS_CA_VERSION_MULTI                    7178
#define         IDS_NETSCAPE_SSL_CLIENT_AUTH            7179
#define         IDS_NETSCAPE_SSL_SERVER_AUTH            7180
#define         IDS_NETSCAPE_SMIME                      7181
#define         IDS_NETSCAPE_SIGN                       7182
#define         IDS_NETSCAPE_SSL_CA                     7183
#define         IDS_NETSCAPE_SMIME_CA                   7184
#define         IDS_NETSCAPE_SIGN_CA                    7185
#define         IDS_UNKNOWN_CERT_TYPE                   7186
#define         IDS_OTHER_NAME_MULTI                    7187
#define         IDS_OTHER_NAME_OIDNAME                  7188
#define         IDS_OTHER_NAME_OID                      7189
#define         IDS_NAME_VALUE                          7190
#define         IDS_NAME_VALUE_MULTI                    7191
#define         IDS_POLICY_QUALIFIER_ELEMENT            7192
#define         IDS_USER_NOTICE_TEXT                    7193                        
#define         IDS_USER_NOTICE_REF_ORG                 7194        
#define         IDS_USER_NOTICE_REF                     7195
#define         IDS_USER_NOTICE_REF_NUMBER              7196
#define         IDS_POLICY_QUALIFIER_NO_BLOB_MULTI      7197
// Post Win2K
#define         IDS_INTEGER                             7198
#define         IDS_STRING                              7199
#define         IDS_GENERIC_OBJECT_ID                   7200
#define         IDS_CRL_NUMBER                          7201
#define         IDS_DELTA_CRL_INDICATOR                 7202
#define         IDS_CRL_VIRTUAL_BASE                    7203
#define         IDS_ONLY_CONTAINS_USER_CERTS            7204
#define         IDS_ONLY_CONTAINS_CA_CERTS              7205
#define         IDS_INDIRECT_CRL                        7206
#define         IDS_ONLY_SOME_CRL_DIST_NAME             7207
#define         IDS_ONLY_SOME_CRL_DIST_NAME_MULTI       7208
#define         IDS_FRESHEST_CRL                        7209
#define         IDS_FRESHEST_CRL_MULTI                  7210
#define         IDS_CRL_SELF_CDP                        7211
#define         IDS_CRL_SELF_CDP_MULTI                  7212
#define         IDS_NAME_CONSTRAINTS_PERMITTED          7213
#define         IDS_NAME_CONSTRAINTS_EXCLUDED           7214
#define         IDS_NAME_CONSTRAINTS_PERMITTED_NONE     7215
#define         IDS_NAME_CONSTRAINTS_EXCLUDED_NONE      7216
#define         IDS_NAME_CONSTRAINTS_SUBTREE            7217
#define         IDS_NAME_CONSTRAINTS_SUBTREE_NO_MAX     7218

#define         IDS_APPLICATION_CERT_POLICY             7219 
#define         IDS_APPLICATION_CERT_POLICY_MULTI       7220 
#define         IDS_APPLICATION_CERT_POLICY_NO_QUA      7221 
#define         IDS_APPLICATION_CERT_POLICY_NO_QUA_MULTI 7222 
#define         IDS_ISSUER_DOMAIN_POLICY                7223
#define         IDS_SUBJECT_DOMAIN_POLICY               7224
#define         IDS_REQUIRED_EXPLICIT_POLICY_SKIP_CERTS 7225
#define         IDS_INHIBIT_POLICY_MAPPING_SKIP_CERTS   7226
#define         IDS_CERTIFICATE_TEMPLATE_MAJOR_VERSION  7227
#define         IDS_CERTIFICATE_TEMPLATE_MINOR_VERSION  7228

#define         IDS_IPADDRESS_V4_4                      7229
#define         IDS_IPADDRESS_V4_8                      7230
#define         IDS_IPADDRESS_V6_16                     7231
#define         IDS_IPADDRESS_V6_32                     7232

#define         IDS_XCERT_DELTA_SYNC_TIME               7233
#define         IDS_XCERT_DIST_POINT                    7234

#define         IDS_HTTP_RESPONSE_STATUS                7235

// CSP resources in range 7500 ... 7600
// THESE RESOURCES ARE LOADED BY THE MS CSPs SO THE
// DEFINE VALUES MAY NOT CHANGE UNLESS THEY ARE
// CHANGED IN THE CSPs
#define         IDS_CSP_RSA_SIG_DESCR                   7501
#define         IDS_CSP_RSA_EXCH_DESCR                  7502
#define         IDS_CSP_IMPORT_SIMPLE                   7503
#define         IDS_CSP_SIGNING_E                       7504
#define         IDS_CSP_CREATE_RSA_SIG                  7505
#define         IDS_CSP_CREATE_RSA_EXCH                 7506
#define         IDS_CSP_DSS_SIG_DESCR                   7507
#define         IDS_CSP_DSS_EXCH_DESCR                  7508
#define         IDS_CSP_CREATE_DSS_SIG                  7509
#define         IDS_CSP_CREATE_DH_EXCH                  7510
#define         IDS_CSP_IMPORT_E_PUB                    7511
#define         IDS_CSP_MIGR                            7512
#define         IDS_CSP_DELETE_SIG                      7513
#define         IDS_CSP_DELETE_KEYX                     7514
#define         IDS_CSP_DELETE_SIG_MIGR                 7515
#define         IDS_CSP_DELETE_KEYX_MIGR                7516
#define         IDS_CSP_SIGNING_S                       7517
#define         IDS_CSP_EXPORT_E_PRIV                   7518
#define         IDS_CSP_EXPORT_S_PRIV                   7519
#define         IDS_CSP_IMPORT_E_PRIV                   7520
#define         IDS_CSP_IMPORT_S_PRIV                   7521
#define         IDS_CSP_AUDIT_CAPI_KEY                  7522

#include "oidinfo.h"
// Note IDS_EXT_*                          in range 8000 .. 8499
// Note IDS_ENHKEY_*                       in range 8500 .. 8999
// Note IDS_SYS_NAME_                      in range 9000 .. 9099
// Note IDS_PHY_NAME_                      in range 9100 .. 9199
