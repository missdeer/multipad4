// Adapted verbatim cache generation from the pinned notepad4 EditAutoC.cpp.
// Original notices and license are retained in 3rdparty/notepad4/License.txt.
#include "CompletionEngine.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include <cstring>
#include <map>
#include "resource.h"
namespace {
#ifdef MULTIPAD4_STATIC_BASELINE
#define NP2_KEYWORDS_COUNT	4
typedef const char * const NP2_KEYWORDS[NP2_KEYWORDS_COUNT];

// Doxygen
static const char * const kwDoxyDoc[NP2_KEYWORDS_COUNT] = {
"a addindex addtogroup anchor arg attention author authors b brief bug c callgraph callergraph category cite class code cond copybrief copydetails copydoc copyright date def defgroup deprecated details dir docbookonly dontinclude dot dotfile e else elseif em endcode endcond enddocbookonly enddot endhtmlonly endif endinternal endlatexonly endlink endmanonly endmsc endrtfonly endsecreflist endverbatim endxmlonly enum example exception extends file fn headerfile hideinitializer htmlinclude htmlonly idlexcept if ifnot image implements include includelineno ingroup internal invariant interface latexonly li line link mainpage manonly memberof msc mscfile n name namespace nosubgrouping note overload p package page par paragraph param post pre private privatesection property protected protectedsection protocol public publicsection pure ref refitem related relates relatedalso relatesalso remark remarks result return returns retval rtfonly sa secreflist section see short showinitializer since skip skipline snippet struct subpage subsection subsubsection tableofcontents test throw throws todo tparam typedef union until var verbatim verbinclude version vhdlflow warning weakgroup xmlonly xrefitem "
, // C#
"inheritdoc list description item term listheader para paramref permission seealso summary typeparam typeparamref value "
, // C# Attribute
"type name cref "
,
nullptr
};

// XML Schema
static const char * const kwXmlSchema[NP2_KEYWORDS_COUNT] = {
"xs xsd schema simpleType complexType complexContent simpleContent extension restriction element unique key keyref "
"selector field group all choice sequence any anyAttribute attribute attributeGroup include import redefine notation "
"annotation appinfo documentation "
//
"atomic enumeration list union pattern whiteSpace length minLength maxLength totalDigits fractionDigits "
"minInclusive maxInclusive minExclusive maxExclusive "
, // attribute
"ref targetNamespace finalDefault blockDefault elementFormDefault attributeFormDefault "
"abstract final block mixed base minOccurs maxOccurs nillable nil substitutionGroup default fixed form refer "
"xpath namespace processContents use schemaLocation public system source itemType memberTypes "
, // value
"anyType anySimpleType duration dateTime time date gYear gYearMonth gMonth gMonthDay gDay "
"boolean base64Binary hexBinary float double anyURI QName "
"decimal integer long int short byte nonPositiveInteger negativeInteger nonNegativeInteger positiveInteger "
"unsignedLong unsignedInt unsignedShort unsignedByte "
"string normalizedString token language Name NCName "
"equal ordered bounded cardinality numeric preserve replace collapse "
"unqualified qualified strict skip lax prohibited optional required "
//"blockSet complexDerivationSet XPathExpr URIref "
, nullptr
};

// XML Stylesheet
static const char * const kwXmlStylesheet[NP2_KEYWORDS_COUNT] = {
"xsl stylesheet transform attribute-set character-map output-character decimal-format import import-schema include "
"key namespace-alias output param preserve-space strip-space template variable "
// instruction
"analyze-string apply-imports apply-templates attribute call-template choose when comment copy copy-of document element "
"fallback for-each for-each-group function if matching-substring message namespace next-match non-matching-substring "
"number otherwise perform-sort processing-instruction result-document sequence sort text value-of with-param "
, // attribute
"extension-element-prefixes exclude-result-prefixes xpath-default-namespace default-validation default-collation "
"input-type-annotations use-attribute-sets use-character-maps character string decimal-separator grouping-separator "
"infinity minus-sign NaN percent per-mille zero-digit digit pattern-separator href namespace schema-location match "
"collation stylesheet-prefix result-prefix method byte-order-mark cdata-section-elements doctype-public doctype-system "
"escape-uri-attributes include-content-type indent media-type normalization-form omit-xml-declaration undeclare-prefixes "
"select as required tunnel elements priority regex flags mode separator test copy-namespaces inherit-namespaces "
"group-by group-adjacent group-starting-with group-ending-with override terminate level count from format letter-value "
"ordinal grouping-size output-version order stable case-order data-type disable-output-escaping "
, // value
"preserve strip unspecified default omit lax single multiple any alphabetic traditional ascending descending "
"upper-first lower-first text number xml html xhtml NFC NFD NFKC NFKD fully-normalized none "
, nullptr
};

// Apache Config
// https://httpd.apache.org/docs/2.2/en/mod/directives.html
// https://httpd.apache.org/docs/2.4/en/mod/directives.html
static const char * const kwApache[NP2_KEYWORDS_COUNT] = {
"AuthnProviderAlias AuthzProviderAlias Directory DirectoryMatch Else ElseIf Files FilesMatch If IfDefine IfModule IfVersion Limit LimitExcept Location LocationMatch Proxy ProxyMatch RequireAll RequireAny RequireNone VirtualHost "
, // Directive
"AcceptFilter AcceptMutex AcceptPathInfo AccessFileName Action AddAlt AddAltByEncoding AddAltByType AddCharset AddDefaultCharset AddDescription AddEncoding AddHandler AddIcon AddIconByEncoding AddIconByType AddInputFilter AddLanguage AddModuleInfo AddOutputFilter AddOutputFilterByType AddType "
"Alias AliasMatch Allow AllowCONNECT AllowEncodedSlashes AllowMethods AllowOverride AllowOverrideList Anonymous Anonymous_LogEmail Anonymous_MustGiveEmail Anonymous_NoUserID Anonymous_VerifyEmail AsyncRequestWorkerFactor "
"AuthBasicAuthoritative AuthBasicProvider AuthDBDUserPWQuery AuthDBDUserRealmQuery AuthDBMGroupFile AuthDBMType AuthDBMUserFile AuthDefaultAuthoritative AuthDigestAlgorithm AuthDigestDomain AuthDigestNcCheck AuthDigestNonceFormat AuthDigestNonceLifetime AuthDigestProvider AuthDigestQop AuthDigestShmemSize AuthFormAuthoritative AuthFormBody AuthFormDisableNoStore AuthFormFakeBasicAuth AuthFormLocation AuthFormLoginRequiredLocation AuthFormLoginSuccessLocation AuthFormLogoutLocation AuthFormMethod AuthFormMimetype AuthFormPassword AuthFormProvider AuthFormSitePassphrase AuthFormSize AuthFormUsername AuthGroupFile "
"AuthLDAPAuthorizePrefix AuthLDAPBindAuthoritative AuthLDAPBindDN AuthLDAPBindPassword AuthLDAPCharsetConfig AuthLDAPCompareAsUser AuthLDAPCompareDNOnServer AuthLDAPDereferenceAliases AuthLDAPGroupAttribute AuthLDAPGroupAttributeIsDN AuthLDAPInitialBindAsUser AuthLDAPInitialBindPattern AuthLDAPMaxSubGroupDepth AuthLDAPRemoteUserAttribute AuthLDAPRemoteUserIsDN AuthLDAPSearchAsUser AuthLDAPSubGroupAttribute AuthLDAPSubGroupClass AuthLDAPUrl "
"AuthMerging AuthName AuthnCacheContext AuthnCacheEnable AuthnCacheProvideFor AuthnCacheSOCache AuthnCacheTimeout AuthType AuthUserFile AuthzDBDLoginToReferer AuthzDBDQuery AuthzDBDRedirectQuery AuthzDBMAuthoritative AuthzDBMType AuthzDefaultAuthoritative AuthzGroupFileAuthoritative AuthzLDAPAuthoritative AuthzOwnerAuthoritative AuthzSendForbiddenOnFailure AuthzUserAuthoritative "
"BalancerGrowth BalancerMember BrowserMatch BrowserMatchNoCase BufferedLogs BufferSize "
"CacheDefaultExpire CacheDetailHeader CacheDirLength CacheDirLevels CacheDisable CacheEnable CacheFile CacheHeader CacheIgnoreCacheControl CacheIgnoreHeaders CacheIgnoreNoLastMod CacheIgnoreQueryString CacheIgnoreURLSessionIdentifiers CacheKeyBaseURL CacheLastModifiedFactor CacheLock CacheLockMaxAge CacheLockPath CacheMaxExpire CacheMaxFileSize CacheMinExpire CacheMinFileSize CacheNegotiatedDocs CacheQuickHandler CacheReadSize CacheReadTime CacheRoot CacheStaleOnError CacheStoreExpired CacheStoreNoStore CacheStorePrivate "
"CGIMapExtension CharsetDefault CharsetOptions CharsetSourceEnc CheckCaseOnly CheckSpelling ChrootDir ContentDigest CookieDomain CookieExpires CookieLog CookieName CookieStyle CookieTracking CoreDumpDirectory CustomLog "
"Dav DavDepthInfinity DavGenericLockDB DavLockDB DavMinTimeout DBDExptime DBDInitSQL DBDKeep DBDMax DBDMin DBDParams DBDPersist DBDPrepareSQL DBDriver DefaultIcon DefaultLanguage DefaultRuntimeDir DefaultType Define DeflateBufferSize DeflateCompressionLevel DeflateFilterNote DeflateMemLevel DeflateWindowSize Deny DirectoryIndex DirectoryIndexRedirect DirectorySlash DocumentRoot DTracePrivileges DumpIOInput DumpIOLogLevel DumpIOOutput "
"EnableExceptionHook EnableMMAP EnableSendfile Error ErrorDocument ErrorLog ErrorLogFormat Example ExpiresActive ExpiresByType ExpiresDefault ExtendedStatus ExtFilterDefine ExtFilterOptions "
"FallbackResource FileETag FilterChain FilterDeclare FilterProtocol FilterProvider FilterTrace ForceLanguagePriority ForceType ForensicLog GprofDir GracefulShutdownTimeout Group Header HeaderName HeartbeatAddress HeartbeatListen HeartbeatMaxServers HeartbeatStorage HostnameLookups "
"IdentityCheck IdentityCheckTimeout ImapBase ImapDefault ImapMenu Include IncludeOptional IndexHeadInsert IndexIgnore IndexIgnoreReset IndexOptions IndexOrderDefault IndexStyleSheet InputSed ISAPIAppendLogToErrors ISAPIAppendLogToQuery ISAPICacheFile ISAPIFakeAsync ISAPILogNotSupported ISAPIReadAheadBuffer KeepAlive KeepAliveTimeout KeptBodySize "
"LanguagePriority LDAPCacheEntries LDAPCacheTTL LDAPConnectionPoolTTL LDAPConnectionTimeout LDAPLibraryDebug LDAPOpCacheEntries LDAPOpCacheTTL LDAPReferralHopLimit LDAPReferrals LDAPRetries LDAPRetryDelay LDAPSharedCacheFile LDAPSharedCacheSize LDAPTimeout LDAPTrustedClientCert LDAPTrustedGlobalCert LDAPTrustedMode LDAPVerifyServerCert "
"LimitInternalRecursion LimitRequestBody LimitRequestFields LimitRequestFieldSize LimitRequestLine LimitXMLRequestBody Listen ListenBackLog LoadFile LoadModule LockFile LogFormat LogLevel LogMessage "
"LuaAuthzProvider LuaHookAccessChecker LuaHookAuthChecker LuaHookCheckUserID LuaHookFixups LuaHookInsertFilter LuaHookMapToStorage LuaHookTranslateName LuaHookTypeChecker LuaInherit LuaPackageCPath LuaPackagePath LuaQuickHandler LuaRoot LuaScope "
"MaxClients MaxConnectionsPerChild MaxKeepAliveRequests MaxMemFree MaxRangeOverlaps MaxRangeReversals MaxRanges MaxRequestsPerChild MaxRequestsPerThread MaxRequestWorkers MaxSpareServers MaxSpareThreads MaxThreads MCacheMaxObjectCount MCacheMaxObjectSize MCacheMaxStreamingBuffer MCacheMinObjectSize MCacheRemovalAlgorithm MCacheSize MetaDir MetaFiles MetaSuffix MimeMagicFile MinSpareServers MinSpareThreads MMapFile ModemStandard ModMimeUsePathInfo MultiviewsMatch Mutex NameVirtualHost NoProxy NWSSLTrustedCerts NWSSLUpgradeable Options Order OutputSed "
"PassEnv PidFile PrivilegesMode Protocol ProtocolEcho ProxyAddHeaders ProxyBadHeader ProxyBlock ProxyDomain ProxyErrorOverride ProxyExpressDBMFile ProxyExpressDBMType ProxyExpressEnable ProxyFtpDirCharset ProxyFtpEscapeWildcards ProxyFtpListOnWildcard ProxyHTMLBufSize ProxyHTMLCharsetOut ProxyHTMLDocType ProxyHTMLEnable ProxyHTMLEvents ProxyHTMLExtended ProxyHTMLFixups ProxyHTMLInterp ProxyHTMLLinks ProxyHTMLMeta ProxyHTMLStripComments ProxyHTMLURLMap ProxyIOBufferSize ProxyMaxForwards ProxyPass ProxyPassInterpolateEnv ProxyPassMatch ProxyPassReverse ProxyPassReverseCookieDomain ProxyPassReverseCookiePath ProxyPreserveHost ProxyReceiveBufferSize ProxyRemote ProxyRemoteMatch ProxyRequests ProxySCGIInternalRedirect ProxySCGISendfile ProxySet ProxySourceAddress ProxyStatus ProxyTimeout ProxyVia "
"ReadmeName ReceiveBufferSize Redirect RedirectMatch RedirectPermanent RedirectTemp ReflectorHeader RemoteIPHeader RemoteIPInternalProxy RemoteIPInternalProxyList RemoteIPProxiesHeader RemoteIPTrustedProxy RemoteIPTrustedProxyList RemoveCharset RemoveEncoding RemoveHandler RemoveInputFilter RemoveLanguage RemoveOutputFilter RemoveType RequestHeader RequestReadTimeout Require RewriteBase RewriteCond RewriteEngine RewriteLock RewriteLog RewriteLogLevel RewriteMap RewriteOptions RewriteRule RLimitCPU RLimitMEM RLimitNPROC "
"Satisfy ScoreBoardFile Script ScriptAlias ScriptAliasMatch ScriptInterpreterSource ScriptLog ScriptLogBuffer ScriptLogLength ScriptSock SecureListen SeeRequestTail SendBufferSize ServerAdmin ServerAlias ServerLimit ServerName ServerPath ServerRoot ServerSignature ServerTokens Session SessionCookieName SessionCookieName2 SessionCookieRemove SessionCryptoCipher SessionCryptoDriver SessionCryptoPassphrase SessionCryptoPassphraseFile SessionDBDCookieName SessionDBDCookieName2 SessionDBDCookieRemove SessionDBDDeleteLabel SessionDBDInsertLabel SessionDBDPerUser SessionDBDSelectLabel SessionDBDUpdateLabel SessionEnv SessionExclude SessionHeader SessionInclude SessionMaxAge SetEnv SetEnvIf SetEnvIfExpr SetEnvIfNoCase SetHandler SetInputFilter SetOutputFilter SSIEnableAccess SSIEndTag SSIErrorMsg SSIETag SSILastModified "
"SSILegacyExprParser SSIStartTag SSITimeFormat SSIUndefinedEcho SSLCACertificateFile SSLCACertificatePath SSLCADNRequestFile SSLCADNRequestPath SSLCARevocationCheck SSLCARevocationFile SSLCARevocationPath SSLCertificateChainFile SSLCertificateFile SSLCertificateKeyFile SSLCipherSuite SSLCompression SSLCryptoDevice SSLEngine SSLFIPS SSLHonorCipherOrder SSLInsecureRenegotiation SSLMutex SSLOCSPDefaultResponder SSLOCSPEnable SSLOCSPOverrideResponder SSLOCSPResponderTimeout SSLOCSPResponseMaxAge SSLOCSPResponseTimeSkew SSLOptions SSLPassPhraseDialog SSLProtocol "
"SSLProxyCACertificateFile SSLProxyCACertificatePath SSLProxyCARevocationCheck SSLProxyCARevocationFile SSLProxyCARevocationPath SSLProxyCheckPeerCN SSLProxyCheckPeerExpire SSLProxyCipherSuite SSLProxyEngine SSLProxyMachineCertificateChainFile SSLProxyMachineCertificateFile SSLProxyMachineCertificatePath SSLProxyProtocol SSLProxyVerify SSLProxyVerifyDepth "
"SSLRandomSeed SSLRenegBufferSize SSLRequire SSLRequireSSL SSLSessionCache SSLSessionCacheTimeout SSLSessionTicketKeyFile SSLStaplingCache SSLStaplingErrorCacheTimeout SSLStaplingFakeTryLater SSLStaplingForceURL SSLStaplingResponderTimeout SSLStaplingResponseMaxAge SSLStaplingResponseTimeSkew SSLStaplingReturnResponderErrors SSLStaplingStandardCacheTimeout SSLStrictSNIVHostCheck SSLUserName SSLUseStapling SSLVerifyClient SSLVerifyDepth StartServers StartThreads Substitute Suexec SuexecUserGroup "
"ThreadLimit ThreadsPerChild ThreadStackSize TimeOut TraceEnable TransferLog TypesConfig UnDefine UnsetEnv UseCanonicalName UseCanonicalPhysicalPort User UserDir VHostCGIMode VHostCGIPrivs VHostGroup VHostPrivs VHostSecure VHostUser VirtualDocumentRoot VirtualDocumentRootIP VirtualScriptAlias VirtualScriptAliasIP WatchdogInterval Win32DisableAcceptEx XBitHack xml2EncAlias xml2EncDefault xml2StartParse"
, // Valves
nullptr
, nullptr
};

// Apple Property List
static const char * const kwPList[NP2_KEYWORDS_COUNT] = {
"plist array data date dict key real integer string "
, nullptr, nullptr, nullptr
};


// SVG Document



#endif
constexpr uint32_t DefaultWordCharSet[8]={0x00000000U,0x03ff0000U,0x87fffffeU,0x07fffffeU,0xffffffffU,0xffffffffU,0xffffffffU,0xffffffffU};
}
void CompletionCache::Initialize(const LanguageDefinition* pLex) noexcept {
	memset(CharacterPrefixMask, 0, sizeof(CharacterPrefixMask));
	memset(RawStringStyleMask, 0, sizeof(RawStringStyleMask));
	memset(GenericTypeStyleMask, 0, sizeof(GenericTypeStyleMask));
	memset(IgnoreWordStyleMask, 0, sizeof(IgnoreWordStyleMask));
	memset(CommentStyleMask, 0, sizeof(CommentStyleMask));
	memset(AllStringStyleMask, 0, sizeof(AllStringStyleMask));
	memset(PlainTextStyleMask, 0, sizeof(PlainTextStyleMask));
	memcpy(CurrentWordCharSet, DefaultWordCharSet, sizeof(DefaultWordCharSet));

	switch (pLex->rid) {
//Cache++Autogenerated -- start of section automatically generated
	case NP2LEX_2NDTEXTFILE:
	case NP2LEX_ANSI:
	case NP2LEX_BATCH:
	case NP2LEX_BLOCKDIAG:
	case NP2LEX_CSV:
	case NP2LEX_GRAPHVIZ:
	case NP2LEX_LISP:
	case NP2LEX_SMALI:
	case NP2LEX_TEXTFILE:
	case NP2LEX_WINHEX:
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		break;

	case NP2LEX_ACTIONSCRIPT:
	case NP2LEX_TYPESCRIPT:
		CurrentWordCharSet['#' >> 5] |= (1 << ('#' & 31));
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		GenericTypeStyleMask[SCE_JS_CLASS >> 5] |= (1U << (SCE_JS_CLASS & 31));
		GenericTypeStyleMask[SCE_JS_INTERFACE >> 5] |= (1U << (SCE_JS_INTERFACE & 31));
		GenericTypeStyleMask[SCE_JS_ENUM >> 5] |= (1U << (SCE_JS_ENUM & 31));
		GenericTypeStyleMask[SCE_JS_WORD2 >> 5] |= (1U << (SCE_JS_WORD2 & 31));
		break;

	case NP2LEX_AUTOIT3:
	case NP2LEX_CIL:
	case NP2LEX_SCALA:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		break;

	case NP2LEX_AWK:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		break;

	case NP2LEX_BASH:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		RawStringStyleMask[SCE_SH_STRING_SQ >> 5] |= (1U << (SCE_SH_STRING_SQ & 31));
		break;

	case NP2LEX_CANGJIE:
		CharacterPrefixMask['r' >> 5] |= (1 << ('r' & 31));
		RawStringStyleMask[SCE_CANGJIE_RAWSTRING_SQ >> 5] |= (1U << (SCE_CANGJIE_RAWSTRING_SQ & 31));
		RawStringStyleMask[SCE_CANGJIE_RAWSTRING_DQ >> 5] |= (1U << (SCE_CANGJIE_RAWSTRING_DQ & 31));
		GenericTypeStyleMask[SCE_CANGJIE_CLASS >> 5] |= (1U << (SCE_CANGJIE_CLASS & 31));
		GenericTypeStyleMask[SCE_CANGJIE_INTERFACE >> 5] |= (1U << (SCE_CANGJIE_INTERFACE & 31));
		GenericTypeStyleMask[SCE_CANGJIE_STRUCT >> 5] |= (1U << (SCE_CANGJIE_STRUCT & 31));
		GenericTypeStyleMask[SCE_CANGJIE_ENUM >> 5] |= (1U << (SCE_CANGJIE_ENUM & 31));
		GenericTypeStyleMask[SCE_CANGJIE_WORD2 >> 5] |= (1U << (SCE_CANGJIE_WORD2 & 31));
		break;

	case NP2LEX_CPP:
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		CharacterPrefixMask['L' >> 5] |= (1 << ('L' & 31));
		CharacterPrefixMask['U' >> 5] |= (1 << ('U' & 31));
		CharacterPrefixMask['u' >> 5] |= (1 << ('u' & 31));
		RawStringStyleMask[SCE_C_STRINGRAW >> 5] |= (1U << (SCE_C_STRINGRAW & 31));
		RawStringStyleMask[SCE_C_COMMENTDOC_TAG >> 5] |= (1U << (SCE_C_COMMENTDOC_TAG & 31));
		GenericTypeStyleMask[SCE_C_CLASS >> 5] |= (1U << (SCE_C_CLASS & 31));
		GenericTypeStyleMask[SCE_C_INTERFACE >> 5] |= (1U << (SCE_C_INTERFACE & 31));
		GenericTypeStyleMask[SCE_C_STRUCT >> 5] |= (1U << (SCE_C_STRUCT & 31));
		GenericTypeStyleMask[SCE_C_WORD2 >> 5] |= (1U << (SCE_C_WORD2 & 31));
		IgnoreWordStyleMask[SCE_C_WORD >> 5] |= (1U << (SCE_C_WORD & 31));
		IgnoreWordStyleMask[SCE_C_WORD2 >> 5] |= (1U << (SCE_C_WORD2 & 31));
		IgnoreWordStyleMask[SCE_C_PREPROCESSOR >> 5] |= (1U << (SCE_C_PREPROCESSOR & 31));
		IgnoreWordStyleMask[SCE_C_ASM_REGISTER >> 5] |= (1U << (SCE_C_ASM_REGISTER & 31));
		IgnoreWordStyleMask[SCE_C_ASM_INSTRUCTION >> 5] |= (1U << (SCE_C_ASM_INSTRUCTION & 31));
		#ifdef MULTIPAD4_STATIC_BASELINE
        extraKeywords = kwDoxyDoc;
#else
        extraKeywords = pLex->completionExtras.data();
#endif
		break;

	case NP2LEX_CSHARP:
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		RawStringStyleMask[SCE_CSHARP_VERBATIM_STRING >> 5] |= (1U << (SCE_CSHARP_VERBATIM_STRING & 31));
		RawStringStyleMask[SCE_CSHARP_INTERPOLATED_VERBATIM_STRING >> 5] |= (1U << (SCE_CSHARP_INTERPOLATED_VERBATIM_STRING & 31));
		RawStringStyleMask[SCE_CSHARP_RAWSTRING_SL >> 5] |= (1U << (SCE_CSHARP_RAWSTRING_SL & 31));
		RawStringStyleMask[SCE_CSHARP_INTERPOLATED_RAWSTRING_SL >> 5] |= (1U << (SCE_CSHARP_INTERPOLATED_RAWSTRING_SL & 31));
		RawStringStyleMask[SCE_CSHARP_RAWSTRING_ML >> 5] |= (1U << (SCE_CSHARP_RAWSTRING_ML & 31));
		RawStringStyleMask[SCE_CSHARP_INTERPOLATED_RAWSTRING_ML >> 5] |= (1U << (SCE_CSHARP_INTERPOLATED_RAWSTRING_ML & 31));
		GenericTypeStyleMask[SCE_CSHARP_CLASS >> 5] |= (1U << (SCE_CSHARP_CLASS & 31));
		GenericTypeStyleMask[SCE_CSHARP_INTERFACE >> 5] |= (1U << (SCE_CSHARP_INTERFACE & 31));
		GenericTypeStyleMask[SCE_CSHARP_STRUCT >> 5] |= (1U << (SCE_CSHARP_STRUCT & 31));
		GenericTypeStyleMask[SCE_CSHARP_ENUM >> 5] |= (1U << (SCE_CSHARP_ENUM & 31));
		GenericTypeStyleMask[SCE_CSHARP_WORD2 >> 5] |= (1U << (SCE_CSHARP_WORD2 & 31));
		break;

	case NP2LEX_CSS:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		break;

	case NP2LEX_DART:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CharacterPrefixMask['r' >> 5] |= (1 << ('r' & 31));
		RawStringStyleMask[SCE_DART_RAWSTRING_SQ >> 5] |= (1U << (SCE_DART_RAWSTRING_SQ & 31));
		RawStringStyleMask[SCE_DART_RAWSTRING_DQ >> 5] |= (1U << (SCE_DART_RAWSTRING_DQ & 31));
		RawStringStyleMask[SCE_DART_TRIPLE_RAWSTRING_SQ >> 5] |= (1U << (SCE_DART_TRIPLE_RAWSTRING_SQ & 31));
		RawStringStyleMask[SCE_DART_TRIPLE_RAWSTRING_DQ >> 5] |= (1U << (SCE_DART_TRIPLE_RAWSTRING_DQ & 31));
		GenericTypeStyleMask[SCE_DART_CLASS >> 5] |= (1U << (SCE_DART_CLASS & 31));
		GenericTypeStyleMask[SCE_DART_ENUM >> 5] |= (1U << (SCE_DART_ENUM & 31));
		GenericTypeStyleMask[SCE_DART_WORD2 >> 5] |= (1U << (SCE_DART_WORD2 & 31));
		break;

	case NP2LEX_DLANG:
		RawStringStyleMask[SCE_D_RAWSTRING >> 5] |= (1U << (SCE_D_RAWSTRING & 31));
		RawStringStyleMask[SCE_D_STRING_BT >> 5] |= (1U << (SCE_D_STRING_BT & 31));
		break;

	case NP2LEX_ELIXIR:
		RawStringStyleMask[SCE_ERLANG_VERBATIM_SIGIL >> 5] |= (1U << (SCE_ERLANG_VERBATIM_SIGIL & 31));
		RawStringStyleMask[SCE_ERLANG_TRIPLE_SIGIL_SQ >> 5] |= (1U << (SCE_ERLANG_TRIPLE_SIGIL_SQ & 31));
		RawStringStyleMask[SCE_ERLANG_TRIPLE_SIGIL_DQ >> 5] |= (1U << (SCE_ERLANG_TRIPLE_SIGIL_DQ & 31));
		break;

	case NP2LEX_ERLANG:
		RawStringStyleMask[SCE_ERLANG_TRIPLE_STRING_SQ >> 5] |= (1U << (SCE_ERLANG_TRIPLE_STRING_SQ & 31));
		RawStringStyleMask[SCE_ERLANG_TRIPLE_STRING_DQ >> 5] |= (1U << (SCE_ERLANG_TRIPLE_STRING_DQ & 31));
		RawStringStyleMask[SCE_ERLANG_VERBATIM_SIGIL >> 5] |= (1U << (SCE_ERLANG_VERBATIM_SIGIL & 31));
		RawStringStyleMask[SCE_ERLANG_TRIPLE_SIGIL_SQ >> 5] |= (1U << (SCE_ERLANG_TRIPLE_SIGIL_SQ & 31));
		RawStringStyleMask[SCE_ERLANG_TRIPLE_SIGIL_DQ >> 5] |= (1U << (SCE_ERLANG_TRIPLE_SIGIL_DQ & 31));
		break;

	case NP2LEX_FORTRAN:
		CurrentWordCharSet['%' >> 5] |= (1 << ('%' & 31));
		CharacterPrefixMask['B' >> 5] |= (1 << ('B' & 31));
		CharacterPrefixMask['O' >> 5] |= (1 << ('O' & 31));
		CharacterPrefixMask['Z' >> 5] |= (1 << ('Z' & 31));
		CharacterPrefixMask['b' >> 5] |= (1 << ('b' & 31));
		CharacterPrefixMask['o' >> 5] |= (1 << ('o' & 31));
		CharacterPrefixMask['z' >> 5] |= (1 << ('z' & 31));
		break;

	case NP2LEX_FSHARP:
		RawStringStyleMask[SCE_FSHARP_VERBATIM_STRING >> 5] |= (1U << (SCE_FSHARP_VERBATIM_STRING & 31));
		RawStringStyleMask[SCE_FSHARP_INTERPOLATED_VERBATIM_STRING >> 5] |= (1U << (SCE_FSHARP_INTERPOLATED_VERBATIM_STRING & 31));
		RawStringStyleMask[SCE_FSHARP_TRIPLE_STRING >> 5] |= (1U << (SCE_FSHARP_TRIPLE_STRING & 31));
		RawStringStyleMask[SCE_FSHARP_INTERPOLATED_TRIPLE_STRING >> 5] |= (1U << (SCE_FSHARP_INTERPOLATED_TRIPLE_STRING & 31));
		break;

	case NP2LEX_GN:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		break;

	case NP2LEX_GO:
		RawStringStyleMask[SCE_GO_RAW_STRING >> 5] |= (1U << (SCE_GO_RAW_STRING & 31));
		break;

	case NP2LEX_GRADLE:
	case NP2LEX_GROOVY:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		GenericTypeStyleMask[SCE_GROOVY_CLASS >> 5] |= (1U << (SCE_GROOVY_CLASS & 31));
		GenericTypeStyleMask[SCE_GROOVY_INTERFACE >> 5] |= (1U << (SCE_GROOVY_INTERFACE & 31));
		GenericTypeStyleMask[SCE_GROOVY_TRAIT >> 5] |= (1U << (SCE_GROOVY_TRAIT & 31));
		GenericTypeStyleMask[SCE_GROOVY_ENUM >> 5] |= (1U << (SCE_GROOVY_ENUM & 31));
		break;

	case NP2LEX_HAXE:
		GenericTypeStyleMask[SCE_HAXE_CLASS >> 5] |= (1U << (SCE_HAXE_CLASS & 31));
		GenericTypeStyleMask[SCE_HAXE_INTERFACE >> 5] |= (1U << (SCE_HAXE_INTERFACE & 31));
		GenericTypeStyleMask[SCE_HAXE_ENUM >> 5] |= (1U << (SCE_HAXE_ENUM & 31));
		break;

	case NP2LEX_HTML:
	case NP2LEX_XML:
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		CommentStyleMask[SCE_H_COMMENT >> 5] |= (1U << (SCE_H_COMMENT & 31));
		CommentStyleMask[SCE_H_XCCOMMENT >> 5] |= (1U << (SCE_H_XCCOMMENT & 31));
		CommentStyleMask[SCE_H_SGML_COMMENT >> 5] |= (1U << (SCE_H_SGML_COMMENT & 31));
		CommentStyleMask[SCE_H_SGML_1ST_PARAM_COMMENT >> 5] |= (1U << (SCE_H_SGML_1ST_PARAM_COMMENT & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTLINE) >> 5] |= (1U << (js_style(SCE_JS_COMMENTLINE) & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTBLOCK) >> 5] |= (1U << (js_style(SCE_JS_COMMENTBLOCK) & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTBLOCKDOC) >> 5] |= (1U << (js_style(SCE_JS_COMMENTBLOCKDOC) & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTTAGAT) >> 5] |= (1U << (js_style(SCE_JS_COMMENTTAGAT) & 31));
		CommentStyleMask[js_style(SCE_JS_TASKMARKER) >> 5] |= (1U << (js_style(SCE_JS_TASKMARKER) & 31));
		CommentStyleMask[css_style(SCE_CSS_COMMENTBLOCK) >> 5] |= (1U << (css_style(SCE_CSS_COMMENTBLOCK) & 31));
		CommentStyleMask[css_style(SCE_CSS_CDO_CDC) >> 5] |= (1U << (css_style(SCE_CSS_CDO_CDC) & 31));
		CommentStyleMask[SCE_HB_COMMENTLINE >> 5] |= (1U << (SCE_HB_COMMENTLINE & 31));
		AllStringStyleMask[SCE_H_DOUBLESTRING >> 5] |= (1U << (SCE_H_DOUBLESTRING & 31));
		AllStringStyleMask[SCE_H_SINGLESTRING >> 5] |= (1U << (SCE_H_SINGLESTRING & 31));
		AllStringStyleMask[SCE_H_SGML_DOUBLESTRING >> 5] |= (1U << (SCE_H_SGML_DOUBLESTRING & 31));
		AllStringStyleMask[SCE_H_SGML_SIMPLESTRING >> 5] |= (1U << (SCE_H_SGML_SIMPLESTRING & 31));
		AllStringStyleMask[js_style(SCE_JS_STRING_SQ) >> 5] |= (1U << (js_style(SCE_JS_STRING_SQ) & 31));
		AllStringStyleMask[js_style(SCE_JS_STRING_DQ) >> 5] |= (1U << (js_style(SCE_JS_STRING_DQ) & 31));
		AllStringStyleMask[js_style(SCE_JS_TEMPLATELITERAL) >> 5] |= (1U << (js_style(SCE_JS_TEMPLATELITERAL) & 31));
		AllStringStyleMask[js_style(SCE_JS_REGEX) >> 5] |= (1U << (js_style(SCE_JS_REGEX) & 31));
		AllStringStyleMask[js_style(SCE_JS_ESCAPECHAR) >> 5] |= (1U << (js_style(SCE_JS_ESCAPECHAR) & 31));
		AllStringStyleMask[css_style(SCE_CSS_ESCAPECHAR) >> 5] |= (1U << (css_style(SCE_CSS_ESCAPECHAR) & 31));
		AllStringStyleMask[css_style(SCE_CSS_STRING_SQ) >> 5] |= (1U << (css_style(SCE_CSS_STRING_SQ) & 31));
		AllStringStyleMask[css_style(SCE_CSS_STRING_DQ) >> 5] |= (1U << (css_style(SCE_CSS_STRING_DQ) & 31));
		AllStringStyleMask[css_style(SCE_CSS_URL) >> 5] |= (1U << (css_style(SCE_CSS_URL) & 31));
		AllStringStyleMask[SCE_HB_STRING >> 5] |= (1U << (SCE_HB_STRING & 31));
		PlainTextStyleMask[SCE_H_DEFAULT >> 5] |= (1U << (SCE_H_DEFAULT & 31));
		break;

	case NP2LEX_JAVA:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		GenericTypeStyleMask[SCE_JAVA_CLASS >> 5] |= (1U << (SCE_JAVA_CLASS & 31));
		GenericTypeStyleMask[SCE_JAVA_INTERFACE >> 5] |= (1U << (SCE_JAVA_INTERFACE & 31));
		GenericTypeStyleMask[SCE_JAVA_ENUM >> 5] |= (1U << (SCE_JAVA_ENUM & 31));
		break;

	case NP2LEX_JAVASCRIPT:
		CurrentWordCharSet['#' >> 5] |= (1 << ('#' & 31));
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		break;

	case NP2LEX_JULIA:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		RawStringStyleMask[SCE_JULIA_RAWSTRING >> 5] |= (1U << (SCE_JULIA_RAWSTRING & 31));
		RawStringStyleMask[SCE_JULIA_TRIPLE_RAWSTRING >> 5] |= (1U << (SCE_JULIA_TRIPLE_RAWSTRING & 31));
		break;

	case NP2LEX_KOTLIN:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		RawStringStyleMask[SCE_KOTLIN_RAWSTRING >> 5] |= (1U << (SCE_KOTLIN_RAWSTRING & 31));
		GenericTypeStyleMask[SCE_KOTLIN_CLASS >> 5] |= (1U << (SCE_KOTLIN_CLASS & 31));
		GenericTypeStyleMask[SCE_KOTLIN_INTERFACE >> 5] |= (1U << (SCE_KOTLIN_INTERFACE & 31));
		GenericTypeStyleMask[SCE_KOTLIN_ENUM >> 5] |= (1U << (SCE_KOTLIN_ENUM & 31));
		break;

	case NP2LEX_LATEX:
		PlainTextStyleMask[SCE_L_DEFAULT >> 5] |= (1U << (SCE_L_DEFAULT & 31));
		PlainTextStyleMask[SCE_L_VERBATIM2 >> 5] |= (1U << (SCE_L_VERBATIM2 & 31));
		PlainTextStyleMask[SCE_L_VERBATIM >> 5] |= (1U << (SCE_L_VERBATIM & 31));
		PlainTextStyleMask[SCE_L_TITLE_TOC >> 5] |= (1U << (SCE_L_TITLE_TOC & 31));
		PlainTextStyleMask[SCE_L_TITLE >> 5] |= (1U << (SCE_L_TITLE & 31));
		PlainTextStyleMask[SCE_L_CHAPTER >> 5] |= (1U << (SCE_L_CHAPTER & 31));
		PlainTextStyleMask[SCE_L_SECTION >> 5] |= (1U << (SCE_L_SECTION & 31));
		PlainTextStyleMask[SCE_L_SECTION1 >> 5] |= (1U << (SCE_L_SECTION1 & 31));
		PlainTextStyleMask[SCE_L_SECTION2 >> 5] |= (1U << (SCE_L_SECTION2 & 31));
		break;

	case NP2LEX_LLVM:
	case NP2LEX_WASM:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['%' >> 5] |= (1 << ('%' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		break;

	case NP2LEX_LUA:
		RawStringStyleMask[SCE_LUA_LITERALSTRING >> 5] |= (1U << (SCE_LUA_LITERALSTRING & 31));
		break;

	case NP2LEX_MAKEFILE:
	case NP2LEX_NSIS:
		CurrentWordCharSet['!' >> 5] |= (1 << ('!' & 31));
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		break;

	case NP2LEX_MARKDOWN:
		CommentStyleMask[SCE_H_COMMENT >> 5] |= (1U << (SCE_H_COMMENT & 31));
		CommentStyleMask[SCE_H_SGML_COMMENT >> 5] |= (1U << (SCE_H_SGML_COMMENT & 31));
		CommentStyleMask[STYLE_COMMENT_LINK >> 5] |= (1U << (STYLE_COMMENT_LINK & 31));
		AllStringStyleMask[SCE_H_DOUBLESTRING >> 5] |= (1U << (SCE_H_DOUBLESTRING & 31));
		AllStringStyleMask[SCE_H_SINGLESTRING >> 5] |= (1U << (SCE_H_SINGLESTRING & 31));
		AllStringStyleMask[SCE_H_SGML_DOUBLESTRING >> 5] |= (1U << (SCE_H_SGML_DOUBLESTRING & 31));
		AllStringStyleMask[SCE_H_SGML_SIMPLESTRING >> 5] |= (1U << (SCE_H_SGML_SIMPLESTRING & 31));
		PlainTextStyleMask[SCE_H_DEFAULT >> 5] |= (1U << (SCE_H_DEFAULT & 31));
		PlainTextStyleMask[1] = UINT32_MAX;
		PlainTextStyleMask[2] = UINT32_MAX;
		PlainTextStyleMask[3] = UINT32_MAX;
		break;

	case NP2LEX_NIM:
		RawStringStyleMask[SCE_NIM_RAWSTRING >> 5] |= (1U << (SCE_NIM_RAWSTRING & 31));
		RawStringStyleMask[SCE_NIM_RAWFMTSTRING >> 5] |= (1U << (SCE_NIM_RAWFMTSTRING & 31));
		RawStringStyleMask[SCE_NIM_TRIPLE_STRING >> 5] |= (1U << (SCE_NIM_TRIPLE_STRING & 31));
		RawStringStyleMask[SCE_NIM_TRIPLE_FMTSTRING >> 5] |= (1U << (SCE_NIM_TRIPLE_FMTSTRING & 31));
		break;

	case NP2LEX_OCAML:
		RawStringStyleMask[SCE_OCAML_QUOTED_STRING >> 5] |= (1U << (SCE_OCAML_QUOTED_STRING & 31));
		break;

	case NP2LEX_PERL:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		RawStringStyleMask[SCE_PL_STRING_SQ >> 5] |= (1U << (SCE_PL_STRING_SQ & 31));
		PlainTextStyleMask[SCE_PL_POD >> 5] |= (1U << (SCE_PL_POD & 31));
		PlainTextStyleMask[SCE_PL_POD_VERB >> 5] |= (1U << (SCE_PL_POD_VERB & 31));
		PlainTextStyleMask[SCE_PL_DATASECTION >> 5] |= (1U << (SCE_PL_DATASECTION & 31));
		break;

	case NP2LEX_PHP:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		RawStringStyleMask[SCE_PHP_STRING_SQ >> 5] |= (1U << (SCE_PHP_STRING_SQ & 31));
		RawStringStyleMask[SCE_PHP_NOWDOC >> 5] |= (1U << (SCE_PHP_NOWDOC & 31));
		CommentStyleMask[SCE_H_COMMENT >> 5] |= (1U << (SCE_H_COMMENT & 31));
		CommentStyleMask[SCE_H_SGML_COMMENT >> 5] |= (1U << (SCE_H_SGML_COMMENT & 31));
		CommentStyleMask[SCE_PHP_COMMENTLINE >> 5] |= (1U << (SCE_PHP_COMMENTLINE & 31));
		CommentStyleMask[SCE_PHP_COMMENTBLOCK >> 5] |= (1U << (SCE_PHP_COMMENTBLOCK & 31));
		CommentStyleMask[SCE_PHP_COMMENTBLOCKDOC >> 5] |= (1U << (SCE_PHP_COMMENTBLOCKDOC & 31));
		CommentStyleMask[SCE_PHP_COMMENTTAGAT >> 5] |= (1U << (SCE_PHP_COMMENTTAGAT & 31));
		CommentStyleMask[SCE_PHP_TASKMARKER >> 5] |= (1U << (SCE_PHP_TASKMARKER & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTLINE) >> 5] |= (1U << (js_style(SCE_JS_COMMENTLINE) & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTBLOCK) >> 5] |= (1U << (js_style(SCE_JS_COMMENTBLOCK) & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTBLOCKDOC) >> 5] |= (1U << (js_style(SCE_JS_COMMENTBLOCKDOC) & 31));
		CommentStyleMask[js_style(SCE_JS_COMMENTTAGAT) >> 5] |= (1U << (js_style(SCE_JS_COMMENTTAGAT) & 31));
		CommentStyleMask[js_style(SCE_JS_TASKMARKER) >> 5] |= (1U << (js_style(SCE_JS_TASKMARKER) & 31));
		CommentStyleMask[css_style(SCE_CSS_COMMENTBLOCK) >> 5] |= (1U << (css_style(SCE_CSS_COMMENTBLOCK) & 31));
		CommentStyleMask[css_style(SCE_CSS_CDO_CDC) >> 5] |= (1U << (css_style(SCE_CSS_CDO_CDC) & 31));
		AllStringStyleMask[SCE_H_DOUBLESTRING >> 5] |= (1U << (SCE_H_DOUBLESTRING & 31));
		AllStringStyleMask[SCE_H_SINGLESTRING >> 5] |= (1U << (SCE_H_SINGLESTRING & 31));
		AllStringStyleMask[SCE_H_SGML_DOUBLESTRING >> 5] |= (1U << (SCE_H_SGML_DOUBLESTRING & 31));
		AllStringStyleMask[SCE_H_SGML_SIMPLESTRING >> 5] |= (1U << (SCE_H_SGML_SIMPLESTRING & 31));
		AllStringStyleMask[js_style(SCE_JS_STRING_SQ) >> 5] |= (1U << (js_style(SCE_JS_STRING_SQ) & 31));
		AllStringStyleMask[js_style(SCE_JS_STRING_DQ) >> 5] |= (1U << (js_style(SCE_JS_STRING_DQ) & 31));
		AllStringStyleMask[js_style(SCE_JS_TEMPLATELITERAL) >> 5] |= (1U << (js_style(SCE_JS_TEMPLATELITERAL) & 31));
		AllStringStyleMask[js_style(SCE_JS_REGEX) >> 5] |= (1U << (js_style(SCE_JS_REGEX) & 31));
		AllStringStyleMask[js_style(SCE_JS_ESCAPECHAR) >> 5] |= (1U << (js_style(SCE_JS_ESCAPECHAR) & 31));
		AllStringStyleMask[css_style(SCE_CSS_ESCAPECHAR) >> 5] |= (1U << (css_style(SCE_CSS_ESCAPECHAR) & 31));
		AllStringStyleMask[css_style(SCE_CSS_STRING_SQ) >> 5] |= (1U << (css_style(SCE_CSS_STRING_SQ) & 31));
		AllStringStyleMask[css_style(SCE_CSS_STRING_DQ) >> 5] |= (1U << (css_style(SCE_CSS_STRING_DQ) & 31));
		AllStringStyleMask[css_style(SCE_CSS_URL) >> 5] |= (1U << (css_style(SCE_CSS_URL) & 31));
		PlainTextStyleMask[SCE_H_DEFAULT >> 5] |= (1U << (SCE_H_DEFAULT & 31));
		break;

	case NP2LEX_POWERBUILDER:
		CurrentWordCharSet['#' >> 5] |= (1 << ('#' & 31));
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['%' >> 5] |= (1 << ('%' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		break;

	case NP2LEX_POWERSHELL:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		CurrentWordCharSet['?' >> 5] |= (1U << ('?' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		RawStringStyleMask[SCE_POWERSHELL_STRING_SQ >> 5] |= (1U << (SCE_POWERSHELL_STRING_SQ & 31));
		RawStringStyleMask[SCE_POWERSHELL_HERE_STRING_SQ >> 5] |= (1U << (SCE_POWERSHELL_HERE_STRING_SQ & 31));
		break;

	case NP2LEX_PYTHON:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CharacterPrefixMask['B' >> 5] |= (1 << ('B' & 31));
		CharacterPrefixMask['F' >> 5] |= (1 << ('F' & 31));
		CharacterPrefixMask['R' >> 5] |= (1 << ('R' & 31));
		CharacterPrefixMask['T' >> 5] |= (1 << ('T' & 31));
		CharacterPrefixMask['U' >> 5] |= (1 << ('U' & 31));
		CharacterPrefixMask['b' >> 5] |= (1 << ('b' & 31));
		CharacterPrefixMask['f' >> 5] |= (1 << ('f' & 31));
		CharacterPrefixMask['r' >> 5] |= (1 << ('r' & 31));
		CharacterPrefixMask['t' >> 5] |= (1 << ('t' & 31));
		CharacterPrefixMask['u' >> 5] |= (1 << ('u' & 31));
		RawStringStyleMask[SCE_PY_RAWSTRING_SQ >> 5] |= (1U << (SCE_PY_RAWSTRING_SQ & 31));
		RawStringStyleMask[SCE_PY_RAWSTRING_DQ >> 5] |= (1U << (SCE_PY_RAWSTRING_DQ & 31));
		RawStringStyleMask[SCE_PY_TRIPLE_RAWSTRING_SQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWSTRING_SQ & 31));
		RawStringStyleMask[SCE_PY_TRIPLE_RAWSTRING_DQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWSTRING_DQ & 31));
		RawStringStyleMask[SCE_PY_RAWFMTSTRING_SQ >> 5] |= (1U << (SCE_PY_RAWFMTSTRING_SQ & 31));
		RawStringStyleMask[SCE_PY_RAWFMTSTRING_DQ >> 5] |= (1U << (SCE_PY_RAWFMTSTRING_DQ & 31));
		RawStringStyleMask[SCE_PY_TRIPLE_RAWFMTSTRING_SQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWFMTSTRING_SQ & 31));
		RawStringStyleMask[SCE_PY_TRIPLE_RAWFMTSTRING_DQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWFMTSTRING_DQ & 31));
		RawStringStyleMask[SCE_PY_RAWBYTES_SQ >> 5] |= (1U << (SCE_PY_RAWBYTES_SQ & 31));
		RawStringStyleMask[SCE_PY_RAWBYTES_DQ >> 5] |= (1U << (SCE_PY_RAWBYTES_DQ & 31));
		RawStringStyleMask[SCE_PY_TRIPLE_RAWBYTES_SQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWBYTES_SQ & 31));
		RawStringStyleMask[SCE_PY_TRIPLE_RAWBYTES_DQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWBYTES_DQ & 31));
		IgnoreWordStyleMask[SCE_PY_WORD >> 5] |= (1U << (SCE_PY_WORD & 31));
		IgnoreWordStyleMask[SCE_PY_WORD2 >> 5] |= (1U << (SCE_PY_WORD2 & 31));
		IgnoreWordStyleMask[SCE_PY_BUILTIN_CONSTANT >> 5] |= (1U << (SCE_PY_BUILTIN_CONSTANT & 31));
		IgnoreWordStyleMask[SCE_PY_BUILTIN_FUNCTION >> 5] |= (1U << (SCE_PY_BUILTIN_FUNCTION & 31));
		IgnoreWordStyleMask[SCE_PY_ATTRIBUTE >> 5] |= (1U << (SCE_PY_ATTRIBUTE & 31));
		IgnoreWordStyleMask[SCE_PY_OBJECT_FUNCTION >> 5] |= (1U << (SCE_PY_OBJECT_FUNCTION & 31));
		AllStringStyleMask[SCE_PY_ESCAPECHAR >> 5] |= (1U << (SCE_PY_ESCAPECHAR & 31));
		AllStringStyleMask[SCE_PY_FORMAT_SPECIFIER >> 5] |= (1U << (SCE_PY_FORMAT_SPECIFIER & 31));
		break;

	case NP2LEX_REBOL:
		CurrentWordCharSet['!' >> 5] |= (1 << ('!' & 31));
		CurrentWordCharSet['&' >> 5] |= (1 << ('&' & 31));
		CurrentWordCharSet['*' >> 5] |= (1 << ('*' & 31));
		CurrentWordCharSet['+' >> 5] |= (1 << ('+' & 31));
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		CurrentWordCharSet['=' >> 5] |= (1 << ('=' & 31));
		CurrentWordCharSet['?' >> 5] |= (1U << ('?' & 31));
		CurrentWordCharSet['~' >> 5] |= (1 << ('~' & 31));
		break;

	case NP2LEX_RESOURCESCRIPT:
		CharacterPrefixMask['L' >> 5] |= (1 << ('L' & 31));
		CharacterPrefixMask['U' >> 5] |= (1 << ('U' & 31));
		CharacterPrefixMask['u' >> 5] |= (1 << ('u' & 31));
		RawStringStyleMask[SCE_C_STRINGRAW >> 5] |= (1U << (SCE_C_STRINGRAW & 31));
		RawStringStyleMask[SCE_C_COMMENTDOC_TAG >> 5] |= (1U << (SCE_C_COMMENTDOC_TAG & 31));
		IgnoreWordStyleMask[SCE_C_WORD >> 5] |= (1U << (SCE_C_WORD & 31));
		IgnoreWordStyleMask[SCE_C_WORD2 >> 5] |= (1U << (SCE_C_WORD2 & 31));
		IgnoreWordStyleMask[SCE_C_PREPROCESSOR >> 5] |= (1U << (SCE_C_PREPROCESSOR & 31));
		break;

	case NP2LEX_RLANG:
		CharacterPrefixMask['R' >> 5] |= (1 << ('R' & 31));
		CharacterPrefixMask['r' >> 5] |= (1 << ('r' & 31));
		RawStringStyleMask[SCE_R_RAWSTRING_SQ >> 5] |= (1U << (SCE_R_RAWSTRING_SQ & 31));
		RawStringStyleMask[SCE_R_RAWSTRING_DQ >> 5] |= (1U << (SCE_R_RAWSTRING_DQ & 31));
		break;

	case NP2LEX_RUBY:
		CurrentWordCharSet['!' >> 5] |= (1 << ('!' & 31));
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['?' >> 5] |= (1U << ('?' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		RawStringStyleMask[SCE_RB_STRING_SQ >> 5] |= (1U << (SCE_RB_STRING_SQ & 31));
		PlainTextStyleMask[SCE_RB_DATASECTION >> 5] |= (1U << (SCE_RB_DATASECTION & 31));
		break;

	case NP2LEX_RUST:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		CharacterPrefixMask['b' >> 5] |= (1 << ('b' & 31));
		RawStringStyleMask[SCE_RUST_RAW_STRING >> 5] |= (1U << (SCE_RUST_RAW_STRING & 31));
		RawStringStyleMask[SCE_RUST_RAW_BYTESTRING >> 5] |= (1U << (SCE_RUST_RAW_BYTESTRING & 31));
		GenericTypeStyleMask[SCE_RUST_TYPE >> 5] |= (1U << (SCE_RUST_TYPE & 31));
		GenericTypeStyleMask[SCE_RUST_STRUCT >> 5] |= (1U << (SCE_RUST_STRUCT & 31));
		GenericTypeStyleMask[SCE_RUST_TRAIT >> 5] |= (1U << (SCE_RUST_TRAIT & 31));
		GenericTypeStyleMask[SCE_RUST_ENUMERATION >> 5] |= (1U << (SCE_RUST_ENUMERATION & 31));
		GenericTypeStyleMask[SCE_RUST_UNION >> 5] |= (1U << (SCE_RUST_UNION & 31));
		break;

	case NP2LEX_SQL:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		CharacterPrefixMask['B' >> 5] |= (1 << ('B' & 31));
		CharacterPrefixMask['Q' >> 5] |= (1 << ('Q' & 31));
		CharacterPrefixMask['X' >> 5] |= (1 << ('X' & 31));
		CharacterPrefixMask['b' >> 5] |= (1 << ('b' & 31));
		CharacterPrefixMask['q' >> 5] |= (1 << ('q' & 31));
		CharacterPrefixMask['x' >> 5] |= (1 << ('x' & 31));
		RawStringStyleMask[SCE_SQL_QOPERATOR >> 5] |= (1U << (SCE_SQL_QOPERATOR & 31));
		IgnoreWordStyleMask[SCE_SQL_WORD >> 5] |= (1U << (SCE_SQL_WORD & 31));
		IgnoreWordStyleMask[SCE_SQL_WORD2 >> 5] |= (1U << (SCE_SQL_WORD2 & 31));
		IgnoreWordStyleMask[SCE_SQL_USER1 >> 5] |= (1U << (SCE_SQL_USER1 & 31));
		IgnoreWordStyleMask[SCE_SQL_HEX >> 5] |= (1U << (SCE_SQL_HEX & 31));
		IgnoreWordStyleMask[SCE_SQL_HEX2 >> 5] |= (1U << (SCE_SQL_HEX2 & 31));
		break;

	case NP2LEX_SWIFT:
		RawStringStyleMask[SCE_SWIFT_STRING_ED >> 5] |= (1U << (SCE_SWIFT_STRING_ED & 31));
		RawStringStyleMask[SCE_SWIFT_TRIPLE_STRING_ED >> 5] |= (1U << (SCE_SWIFT_TRIPLE_STRING_ED & 31));
		GenericTypeStyleMask[SCE_SWIFT_CLASS >> 5] |= (1U << (SCE_SWIFT_CLASS & 31));
		GenericTypeStyleMask[SCE_SWIFT_STRUCT >> 5] |= (1U << (SCE_SWIFT_STRUCT & 31));
		GenericTypeStyleMask[SCE_SWIFT_PROTOCOL >> 5] |= (1U << (SCE_SWIFT_PROTOCOL & 31));
		GenericTypeStyleMask[SCE_SWIFT_ENUM >> 5] |= (1U << (SCE_SWIFT_ENUM & 31));
		break;

	case NP2LEX_TCL:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet['@' >> 5] |= (1 << ('@' & 31));
		break;

	case NP2LEX_TEXINFO:
		PlainTextStyleMask[SCE_TEXINFO_DEFAULT >> 5] |= (1U << (SCE_TEXINFO_DEFAULT & 31));
		PlainTextStyleMask[SCE_TEXINFO_VERBATIM2 >> 5] |= (1U << (SCE_TEXINFO_VERBATIM2 & 31));
		PlainTextStyleMask[SCE_TEXINFO_VERBATIM >> 5] |= (1U << (SCE_TEXINFO_VERBATIM & 31));
		PlainTextStyleMask[SCE_TEXINFO_TITLE >> 5] |= (1U << (SCE_TEXINFO_TITLE & 31));
		PlainTextStyleMask[SCE_TEXINFO_CHAPTER >> 5] |= (1U << (SCE_TEXINFO_CHAPTER & 31));
		PlainTextStyleMask[SCE_TEXINFO_SECTION >> 5] |= (1U << (SCE_TEXINFO_SECTION & 31));
		PlainTextStyleMask[SCE_TEXINFO_SECTION1 >> 5] |= (1U << (SCE_TEXINFO_SECTION1 & 31));
		PlainTextStyleMask[SCE_TEXINFO_SECTION2 >> 5] |= (1U << (SCE_TEXINFO_SECTION2 & 31));
		break;

	case NP2LEX_TYPST:
		CurrentWordCharSet['-' >> 5] |= (1 << ('-' & 31));
		PlainTextStyleMask[SCE_TYPST_DEFAULT >> 5] |= (1U << (SCE_TYPST_DEFAULT & 31));
		PlainTextStyleMask[SCE_TYPST_HEADER1 >> 5] |= (1U << (SCE_TYPST_HEADER1 & 31));
		PlainTextStyleMask[SCE_TYPST_HEADER2 >> 5] |= (1U << (SCE_TYPST_HEADER2 & 31));
		PlainTextStyleMask[SCE_TYPST_HEADER3 >> 5] |= (1U << (SCE_TYPST_HEADER3 & 31));
		PlainTextStyleMask[SCE_TYPST_HEADER4 >> 5] |= (1U << (SCE_TYPST_HEADER4 & 31));
		PlainTextStyleMask[SCE_TYPST_HEADER5 >> 5] |= (1U << (SCE_TYPST_HEADER5 & 31));
		PlainTextStyleMask[SCE_TYPST_HEADER6 >> 5] |= (1U << (SCE_TYPST_HEADER6 & 31));
		break;

	case NP2LEX_VERILOG:
		CurrentWordCharSet['$' >> 5] |= (1 << ('$' & 31));
		CurrentWordCharSet[':' >> 5] |= (1 << (':' & 31));
		break;

	case NP2LEX_VHDL:
		RawStringStyleMask[SCE_VHDL_STRING >> 5] |= (1U << (SCE_VHDL_STRING & 31));
		break;

	case NP2LEX_VIM:
		RawStringStyleMask[SCE_VIM_STRING_SQ >> 5] |= (1U << (SCE_VIM_STRING_SQ & 31));
		break;

	case NP2LEX_ZIG:
		RawStringStyleMask[SCE_ZIG_MULTISTRING >> 5] |= (1U << (SCE_ZIG_MULTISTRING & 31));
		break;

	default:
		break;

//Cache--Autogenerated -- end of section automatically generated
	}

	CurrentWordCharSet['.' >> 5] |= (1 << ('.' & 31));
	uint32_t marker = pLex->commentStyleMarker;
	if (marker) {
		CommentStyleMask[0] |= (1U << (marker + 1)) - 2;
	}

	marker = pLex->stringStyleLast;
	if (marker) {
		uint32_t start = pLex->stringStyleFirst;
#if 1	// all inside [0, 31] or [40, 63]
		marker = marker - start + 1;
		const uint32_t mask = ((1U << marker) - 1) << (start & 31);
		start >>= 5;
		AllStringStyleMask[start] |= mask;
#else
		++marker;
		do {
			AllStringStyleMask[start >> 5] |= (1U << (start & 31));
			++start;
		} while (start < marker);
#endif
	}


}
const CompletionCache& GetCompletionCache(const LanguageDefinition& lexer){
#ifdef MULTIPAD4_STATIC_BASELINE
    static std::map<int,CompletionCache> caches;
    auto found=caches.find(lexer.rid);if(found!=caches.end())return found->second;
    CompletionCache cache{};cache.Initialize(&lexer);return caches.emplace(lexer.rid,cache).first->second;
#else
    struct Entry { uint64_t version=0; CompletionCache cache; };
    static std::map<std::string,Entry> caches;
    auto& entry=caches[lexer.id];
    if(entry.version!=lexer.rulesVersion){entry.cache={};entry.cache.Initialize(&lexer);entry.cache.owner=lexer.shared_from_this();entry.cache.extraKeywords=lexer.completionExtras.data();entry.version=lexer.rulesVersion;}
    return entry.cache;
#endif
}

const char* const* GetCompletionExtraKeywords(int command){
#ifdef MULTIPAD4_STATIC_BASELINE
    switch(command){case IDM_LEXER_XSD:return kwXmlSchema;case IDM_LEXER_XSLT:return kwXmlStylesheet;case IDM_LEXER_APACHE:return kwApache;case IDM_LEXER_PROPERTY_LIST:return kwPList;default:return nullptr;}
#else
    return ConfigurationDialectWords(command);
#endif
}
