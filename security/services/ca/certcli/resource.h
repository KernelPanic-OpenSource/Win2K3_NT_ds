//+--------------------------------------------------------------------------
//
// Microsoft Windows
// Copyright (C) Microsoft Corporation, 1997 - 1999
//
// File:        resource.h
//
// Contents:    CertCli implementation
//
//---------------------------------------------------------------------------

#define IDS_COLUMN_REQUESTID				101
#define IDS_COLUMN_REQUESTRAWREQUEST			102
#define IDS_COLUMN_REQUESTRAWOLDCERTIFICATE		103
#define IDS_COLUMN_REQUESTATTRIBUTES			104
#define IDS_COLUMN_REQUESTTYPE				105
#define IDS_COLUMN_REQUESTFLAGS				106
#define IDS_COLUMN_REQUESTSTATUS			107
#define IDS_COLUMN_REQUESTSTATUSCODE			108
#define IDS_COLUMN_REQUESTDISPOSITION			109
#define IDS_COLUMN_REQUESTDISPOSITIONMESSAGE		110
#define IDS_COLUMN_REQUESTSUBMITTEDWHEN			111
#define IDS_COLUMN_REQUESTRESOLVEDWHEN			112
#define IDS_COLUMN_REQUESTREVOKEDWHEN			113
#define IDS_COLUMN_REQUESTREVOKEDEFFECTIVEWHEN		114
#define IDS_COLUMN_REQUESTREVOKEDREASON			115
#define IDS_COLUMN_REQUESTERNAME			116
#define IDS_COLUMN_REQUESTERADDRESS			117

#define IDS_COLUMN_REQUESTDISTINGUISHEDNAME		118
#define IDS_COLUMN_REQUESTRAWNAME			119
#define IDS_COLUMN_REQUESTNAMETYPE			120
#define IDS_COLUMN_REQUESTCOUNTRY			121
#define IDS_COLUMN_REQUESTORGANIZATION			122
#define IDS_COLUMN_REQUESTORGUNIT			123
#define IDS_COLUMN_REQUESTCOMMONNAME			124
#define IDS_COLUMN_REQUESTLOCALITY			125
#define IDS_COLUMN_REQUESTSTATE				126
#define IDS_COLUMN_REQUESTTITLE				127
#define IDS_COLUMN_REQUESTGIVENNAME			128
#define IDS_COLUMN_REQUESTINITIALS			129
#define IDS_COLUMN_REQUESTSURNAME			130
#define IDS_COLUMN_REQUESTDOMAINCOMPONENT		131
#define IDS_COLUMN_REQUESTEMAIL				132
#define IDS_COLUMN_REQUESTSTREETADDRESS			133

#define IDS_COLUMN_CERTIFICATEREQUESTID			134
#define IDS_COLUMN_CERTIFICATERAWCERTIFICATE		135
#define IDS_COLUMN_CERTIFICATECERTIFICATEHASH		136
#define IDS_COLUMN_CERTIFICATETYPE			137
#define IDS_COLUMN_CERTIFICATESERIALNUMBER		138
#define IDS_COLUMN_CERTIFICATEISSUERNAMEID		139
#define IDS_COLUMN_CERTIFICATENOTBEFOREDATE		140
#define IDS_COLUMN_CERTIFICATENOTAFTERDATE		141
#define IDS_COLUMN_CERTIFICATERAWPUBLICKEY		142
#define IDS_COLUMN_CERTIFICATEPUBLICKEYALGORITHM	143
#define IDS_COLUMN_CERTIFICATERAWPUBLICKEYALGORITHMPARAMETERS	144

#define IDS_COLUMN_CERTIFICATEDISTINGUISHEDNAME		145
#define IDS_COLUMN_CERTIFICATERAWNAME			146
#define IDS_COLUMN_CERTIFICATENAMETYPE			147
#define IDS_COLUMN_CERTIFICATECOUNTRY			148
#define IDS_COLUMN_CERTIFICATEORGANIZATION		149
#define IDS_COLUMN_CERTIFICATEORGUNIT			150
#define IDS_COLUMN_CERTIFICATECOMMONNAME		151
#define IDS_COLUMN_CERTIFICATELOCALITY			152
#define IDS_COLUMN_CERTIFICATESTATE			153
#define IDS_COLUMN_CERTIFICATETITLE			154
#define IDS_COLUMN_CERTIFICATEGIVENNAME			155
#define IDS_COLUMN_CERTIFICATEINITIALS			156
#define IDS_COLUMN_CERTIFICATESURNAME			157
#define IDS_COLUMN_CERTIFICATEDOMAINCOMPONENT		158
#define IDS_COLUMN_CERTIFICATEEMAIL			159
#define IDS_COLUMN_CERTIFICATESTREETADDRESS		160
#define IDS_COLUMN_CERTIFICATEUNSTRUCTUREDNAME		161
#define IDS_COLUMN_CERTIFICATEUNSTRUCTUREDADDRESS	162

#define IDS_COLUMN_REQUESTUNSTRUCTUREDNAME		163
#define IDS_COLUMN_REQUESTUNSTRUCTUREDADDRESS		164
#define IDS_COLUMN_REQUESTDEVICESERIALNUMBER		165
#define IDS_COLUMN_CERTIFICATEDEVICESERIALNUMBER	166
#define IDS_FILESHARE_REMARK				167
#define IDS_COLUMN_CERTIFICATERAWSMIMECAPABILITIES	168

#define IDS_COLUMN_EXTREQUESTID				169
#define IDS_COLUMN_EXTNAME				170
#define IDS_COLUMN_EXTFLAGS				171
#define IDS_COLUMN_EXTRAWVALUE				172

#define IDS_COLUMN_ATTRIBREQUESTID			173
#define IDS_COLUMN_ATTRIBNAME				174
#define IDS_COLUMN_ATTRIBVALUE				175

#define IDS_COLUMN_CRLROWID				176
#define IDS_COLUMN_CRLNUMBER				177
#define IDS_COLUMN_CRLMINBASE				178
#define IDS_COLUMN_CRLNAMEID				179
#define IDS_COLUMN_CRLCOUNT				180
#define IDS_COLUMN_CRLTHISUPDATE			181
#define IDS_COLUMN_CRLNEXTUPDATE			182
#define IDS_COLUMN_CRLTHISPUBLISH			183
#define IDS_COLUMN_CRLNEXTPUBLISH			184
#define IDS_COLUMN_CRLEFFECTIVE				185
#define IDS_COLUMN_CRLPROPAGATIONCOMPLETE		186
#define IDS_COLUMN_CRLRAWCRL				187

#define IDS_CAPROP_FILEVERSION				188
#define IDS_CAPROP_PRODUCTVERSION			189
#define IDS_CAPROP_EXITCOUNT				190
#define IDS_CAPROP_EXITDESCRIPTION			191
#define IDS_CAPROP_POLICYDESCRIPTION			192
#define IDS_CAPROP_CANAME				193
#define IDS_CAPROP_SANITIZEDCANAME			194
#define IDS_CAPROP_SHAREDFOLDER				195
#define IDS_CAPROP_PARENTCA				196

#define IDS_CAPROP_CATYPE				197
#define IDS_CAPROP_CASIGCERTCOUNT			198
#define IDS_CAPROP_CASIGCERT				199
#define IDS_CAPROP_CASIGCERTCHAIN			200
#define IDS_CAPROP_CAXCHGCERTCOUNT			201
#define IDS_CAPROP_CAXCHGCERT				202
#define IDS_CAPROP_CAXCHGCERTCHAIN			203
#define IDS_CAPROP_BASECRL				204
#define IDS_CAPROP_DELTACRL				205
#define IDS_CAPROP_CACERTSTATE				206
#define IDS_CAPROP_CRLSTATE				207
#define IDS_CAPROP_CAPROPIDMAX				208

#define IDS_COLUMN_CERTIFICATESUBJECTKEYIDENTIFIER	209

#define IDS_UNKNOWN_ERROR_CODE				210
#define IDS_E_UNEXPECTED				211

#define IDS_SETUP_ERROR_EXPECTED_SECTION_NAME		212
#define IDS_SETUP_ERROR_BAD_SECTION_NAME_LINE		213
#define IDS_SETUP_ERROR_SECTION_NAME_TOO_LONG		214
#define IDS_SETUP_ERROR_GENERAL_SYNTAX			215

#define IDS_SETUP_ERROR_WRONG_INF_STYLE			216
#define IDS_SETUP_ERROR_SECTION_NOT_FOUND		217
#define IDS_SETUP_ERROR_LINE_NOT_FOUND			218

#define IDS_COLUMN_REQUESTRAWARCHIVEDKEY		219
#define IDS_COLUMN_REQUESTKEYRECOVERYHASHES		220

#define IDS_CAPROP_DNSNAME				221
#define IDS_COLUMN_PROPCERTIFICATETEMPLATE		222
#define IDS_COLUMN_REQUESTSIGNERPOLICIES		223
#define IDS_COLUMN_REQUESTSIGNERAPPLICATIONPOLICIES	224
#define IDS_COLUMN_PROPCERTIFICATEENROLLMENTFLAGS	225
#define IDS_COLUMN_PROPCERTIFICATEGENERALFLAGS		226
#define IDS_COLUMN_CERTIFICATEPUBLICKEYLENGTH		227
#define IDS_CAPROP_KRACERTUSEDCOUNT			228
#define IDS_CAPROP_KRACERTCOUNT				229
#define IDS_CAPROP_KRACERT				230
#define IDS_CAPROP_KRACERTSTATE				231
#define IDS_CAPROP_ROLESEPARATIONENABLED		232
#define IDS_CAPROP_ADVANCEDSERVER			233
#define IDS_COLUMN_CRLLASTPUBLISHED			234
#define IDS_COLUMN_CRLPUBLISHATTEMPTS			235
#define IDS_COLUMN_CRLPUBLISHFLAGS			236
#define IDS_COLUMN_CRLPUBLISHSTATUSCODE			237
#define IDS_COLUMN_CRLPUBLISHERROR			238
#define IDS_COLUMN_CALLERNAME				239
#define IDS_CAPROP_TEMPLATES				240
#define IDS_CAPROP_BASECRLPUBLISHSTATUS			241
#define IDS_CAPROP_DELTACRLPUBLISHSTATUS		242
#define IDS_CAPROP_CASIGCERTCRLCHAIN			243
#define IDS_CAPROP_CAXCHGCERTCRLCHAIN			244
#define IDS_COLUMN_CERTIFICATEUPN			245
#define IDS_E_DATA_MISALIGNMENT				246
#define IDS_CAPROP_CACERTSTATUSCODE			247

#define IDS_HTTP_STATUS_CONTINUE			248
#define IDS_HTTP_STATUS_SWITCH_PROTOCOLS		249
#define IDS_HTTP_STATUS_OK				250
#define IDS_HTTP_STATUS_CREATED				251
#define IDS_HTTP_STATUS_ACCEPTED			252
#define IDS_HTTP_STATUS_PARTIAL				253
#define IDS_HTTP_STATUS_NO_CONTENT			254
#define IDS_HTTP_STATUS_RESET_CONTENT			255
#define IDS_HTTP_STATUS_PARTIAL_CONTENT			256
#define IDS_HTTP_STATUS_AMBIGUOUS			257
#define IDS_HTTP_STATUS_MOVED				258
#define IDS_HTTP_STATUS_REDIRECT			259
#define IDS_HTTP_STATUS_REDIRECT_METHOD			260
#define IDS_HTTP_STATUS_NOT_MODIFIED			261
#define IDS_HTTP_STATUS_USE_PROXY			262
#define IDS_HTTP_STATUS_REDIRECT_KEEP_VERB		263
#define IDS_HTTP_STATUS_BAD_REQUEST			264
#define IDS_HTTP_STATUS_DENIED				265
#define IDS_HTTP_STATUS_PAYMENT_REQ			266
#define IDS_HTTP_STATUS_FORBIDDEN			267
#define IDS_HTTP_STATUS_NOT_FOUND			268
#define IDS_HTTP_STATUS_BAD_METHOD			269
#define IDS_HTTP_STATUS_NONE_ACCEPTABLE			270
#define IDS_HTTP_STATUS_PROXY_AUTH_REQ			271
#define IDS_HTTP_STATUS_REQUEST_TIMEOUT			272
#define IDS_HTTP_STATUS_CONFLICT			273
#define IDS_HTTP_STATUS_GONE				274
#define IDS_HTTP_STATUS_LENGTH_REQUIRED			275
#define IDS_HTTP_STATUS_PRECOND_FAILED			276
#define IDS_HTTP_STATUS_REQUEST_TOO_LARGE		277
#define IDS_HTTP_STATUS_URI_TOO_LONG			278
#define IDS_HTTP_STATUS_UNSUPPORTED_MEDIA		279
#define IDS_HTTP_STATUS_RETRY_WITH			280
#define IDS_HTTP_STATUS_SERVER_ERROR			281
#define IDS_HTTP_STATUS_NOT_SUPPORTED			282
#define IDS_HTTP_STATUS_BAD_GATEWAY			283
#define IDS_HTTP_STATUS_SERVICE_UNAVAIL			284
#define IDS_HTTP_STATUS_GATEWAY_TIMEOUT			285
#define IDS_HTTP_STATUS_VERSION_NOT_SUP			286

#define IDS_CAPROP_CAFORWARDCROSSCERT			287
#define IDS_CAPROP_CABACKWARDCROSSCERT			288
#define IDS_CAPROP_CAFORWARDCROSSCERTSTATE		289
#define IDS_CAPROP_CABACKWARDCROSSCERTSTATE		290
#define IDS_CAPROP_CACERTVERSION			291
#define IDS_CAPROP_SANITIZEDCASHORTNAME			292

#define IDS_CERTIFICATE_SERVICES			293
#define	IDS_MSCEP					294
#define	IDS_MSCEP_DES					295
#define IDS_COLUMN_REQUESTOFFICER			296