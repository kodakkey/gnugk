//////////////////////////////////////////////////////////////////
//
// lua.cxx
//
// LUA routing, authentication and accounting policies for the GNU Gatekeeper
//
// Copyright (c) 2012-2020, Jan Willamowius
//
// This work is published under the GNU Public License version 2 (GPLv2)
// see file COPYING for details.
// We also explicitly grant the right to link this code
// with the OpenH323/H323Plus and OpenSSL library.
//
//////////////////////////////////////////////////////////////////

#include "config.h"

#ifdef HAS_LUA

#include "Routing.h"
#include "Toolkit.h"
#include "gk_const.h"
#include "snmp.h"

#include "rasinfo.h"
#include "RasPDU.h"
#include "gkauth.h"
#include "gkacct.h"


extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#define LUA_GNUGKLIBNAME    "gnugk"

static int gnugk_trace(lua_State * L) {
	if ((lua_gettop(L) != 2) || !lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
		lua_pushstring(L, "Incorrect arguments for 'trace(level, 'message')'");
		lua_error(L);
		return 0;
	}

	PTRACE(lua_tonumber(L, 1), "LUA\t" << lua_tostring(L, 2));

	return 0; // no results
}

static int gnugk_get_config_string(lua_State * L) {
	if ((lua_gettop(L) != 3) || !lua_isstring(L, 1) || !lua_isstring(L, 2) || !lua_isstring(L, 3)) {
		lua_pushstring(L, "Incorrect arguments for 'get_config_string('section', 'switch', 'default')'");
		lua_error(L);
		return 0;
	}

    PString result = GkConfig()->GetString(lua_tostring(L, 1), lua_tostring(L, 2), lua_tostring(L, 3));

    lua_pushstring(L, result);
	return 1;   // return 1 result string
}

static int gnugk_get_config_integer(lua_State * L) {
	if ((lua_gettop(L) != 3) || !lua_isstring(L, 1) || !lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
		lua_pushstring(L, "Incorrect arguments for 'get_config_integer('section', 'switch', default)'");
		lua_error(L);
		return 0;
	}

    int result = GkConfig()->GetInteger(lua_tostring(L, 1), lua_tostring(L, 2), lua_tonumber(L, 3));

    lua_pushnumber(L, result);
	return 1;
}

static const luaL_Reg gnugklib[] = {
    {"trace", gnugk_trace },
    {"get_config_string", gnugk_get_config_string },
    {"get_config_integer", gnugk_get_config_integer },
    {NULL, NULL}
};

class LuaBase {
public:
	LuaBase();
	virtual ~LuaBase();

	void InitLua(lua_State ** lua);
    void ShutdownLua(lua_State ** lua);
    bool RunLua(lua_State * lua, const PString & script);

	void SetString(lua_State * lua, const char * name, const char * value);
	PString GetString(lua_State * lua, const char * name) const;
	/* currently unused
	void SetNumber(lua_State * lua, const char * name, double value);
	double GetNumber(lua_State * lua, const char * name) const;
	void SetBoolean(lua_State * lua, const char * name, bool value);
	bool GetBoolean(lua_State * lua, const char * name) const;
	*/
};


LuaBase::LuaBase()
{
}

void LuaBase::InitLua(lua_State ** pLua)
{
	if (pLua && !*pLua) {
		*pLua = luaL_newstate();
		luaL_openlibs(*pLua);
		// register "gnugk" lib
        luaL_newlib(*pLua, gnugklib);
        lua_setglobal(*pLua, LUA_GNUGKLIBNAME);
	}
}

void LuaBase::ShutdownLua(lua_State ** pLua)
{
	if (pLua && *pLua) {
        lua_close(*pLua);
        *pLua = NULL;
	}
}

bool LuaBase::RunLua(lua_State * lua, const PString & script)
{
	if (luaL_loadstring(lua, script) != 0 || lua_pcall(lua, 0, 0, 0) != 0) {
		PTRACE(1, "LUA\tError in LUA script: " << lua_tostring(lua, -1));
		lua_pop(lua, 1);
		return false;
	}
	return true;
}

LuaBase::~LuaBase()
{
}

void LuaBase::SetString(lua_State * lua, const char * name, const char * value)
{
    PTRACE(6, "LUA\tSet String " << name << " = " << value);
	lua_pushstring(lua, value);
	lua_setglobal(lua, name);
}

PString LuaBase::GetString(lua_State * lua, const char * name) const
{
	lua_getglobal(lua, name);
	PString result = lua_tostring(lua, -1);
	lua_pop(lua, 1);
	return result;
}

/*
void LuaBase::SetNumber(lua_State * lua, const char * name, double value)
{
    PTRACE(6, "LUA\tSet Number " << name << " = " << value);
	lua_pushnumber(lua, value);
	lua_setglobal(lua, name);
}

double LuaBase::GetNumber(lua_State * lua, const char * name) const
{
	lua_getglobal(lua, name);
	double result = lua_tonumber(lua, -1);
	lua_pop(lua, 1);
	return result;
}

void LuaBase::SetBoolean(lua_State * lua, const char * name, bool value)
{
    PTRACE(6, "LUA\tSet Boolean" << name << " = " << value);
	lua_pushboolean(lua, value);
	lua_setglobal(lua, name);
}

bool LuaBase::GetBoolean(lua_State * lua, const char * name) const
{
	lua_getglobal(lua, name);
	bool result = lua_toboolean(lua, -1);
	lua_pop(lua, 1);
	return result;
}
*/


namespace Routing {

// a policy to route calls with LUA
class LuaPolicy : public LuaBase, public DynamicPolicy {
public:
	LuaPolicy();
	virtual ~LuaPolicy();

protected:
	virtual void LoadConfig(const PString & instance);

	virtual void RunPolicy(
		/*in */
		const PString & source,
		const PString & calledAlias,
		const PString & calledIP,
		const PString & caller,
		const PString & callingStationId,
		const PString & callid,
		const PString & messageType,
		const PString & clientauthid,
		const PString & language,
		/* out: */
		DestinationRoutes & destination);

protected:
	// script to run
	PString m_script;
};

LuaPolicy::LuaPolicy()
{
	m_name = "Lua";
	m_iniSection = "Routing::Lua";
	m_active = false;
}

void LuaPolicy::LoadConfig(const PString & instance)
{
	m_script = GkConfig()->GetString(m_iniSection, "Script", "");
	if (m_script.IsEmpty()) {
		PString scriptFile = GkConfig()->GetString(m_iniSection, "ScriptFile", "");
		if (!scriptFile.IsEmpty()) {
			PTextFile f(scriptFile, PFile::ReadOnly);
			if (!f.IsOpen()) {
				PTRACE(1, "LUA\tCan't read LUA script " << scriptFile);
			} else {
				PString line;
				while (f.ReadLine(line)) {
					m_script += (line + "\n");
				}
			}
		}
	}

	if (m_script.IsEmpty()) {
		PTRACE(2, m_name << "\tmodule creation failed: no LUA script");
		SNMP_TRAP(4, SNMPError, General, PString(m_name) + " creation failed");
		return;
	}

	m_active = true;
}

LuaPolicy::~LuaPolicy()
{
}

void LuaPolicy::RunPolicy(
		/* in */
		const PString & source,
		const PString & calledAlias,
		const PString & calledIP,
		const PString & caller,
		const PString & callingStationId,
		const PString & callid,
		const PString & messageType,
		const PString & clientauthid,
		const PString & language,
		/* out: */
		DestinationRoutes & destination)
{
	lua_State * lua = NULL;
    InitLua(&lua);

	SetString(lua, "source", source);
	SetString(lua, "calledAlias", calledAlias);
	SetString(lua, "calledIP", calledIP);
	SetString(lua, "caller", caller);
	SetString(lua, "callingStationId", callingStationId);
	SetString(lua, "callid", callid);
	SetString(lua, "messageType", messageType);
	SetString(lua, "clientauthid", clientauthid);
	SetString(lua, "language", language);
	SetString(lua, "destAlias", "");
	SetString(lua, "destIP", "");
	SetString(lua, "action", "");
	SetString(lua, "rejectCode", "");

	if (!RunLua(lua, m_script)) {
        ShutdownLua(&lua);
		return;
	}

	PString action = GetString(lua, "action");
	PString rejectCode = GetString(lua, "rejectCode");
	PString destAlias = GetString(lua, "destAlias");
	PString destIP = GetString(lua, "destIP");

	if (action.ToUpper() == "SKIP") {
		PTRACE(5, m_name << "\tSkipping to next policy");
        ShutdownLua(&lua);
		return;
	}

	if (action.ToUpper() == "REJECT") {
		PTRACE(5, m_name << "\tRejecting call");
		destination.SetRejectCall(true);
		if (!rejectCode.IsEmpty()) {
			destination.SetRejectReason(rejectCode.AsInteger());
		}
        ShutdownLua(&lua);
		return;
	}

	if (!destAlias.IsEmpty()) {
		PTRACE(5, m_name << "\tSet new destination alias " << destAlias);
		H225_ArrayOf_AliasAddress newAliases;
		newAliases.SetSize(1);
		H323SetAliasAddress(destAlias, newAliases[0]);
		destination.SetNewAliases(newAliases);
	}

	if (!destIP.IsEmpty()) {
		PTRACE(5, m_name << "\tSet new destination IP " << destIP);
		PStringArray adr_parts = SplitIPAndPort(destIP, GK_DEF_ENDPOINT_SIGNAL_PORT);
		PIPSocket::Address ip(adr_parts[0]);
		WORD port = (WORD)(adr_parts[1].AsInteger());

		Route route("Lua", SocketToH225TransportAddr(ip, port));
		route.m_destEndpoint = RegistrationTable::Instance()->FindBySignalAdr(route.m_destAddr);
		if (!destAlias.IsEmpty())
			route.m_destNumber = destAlias;
		destination.AddRoute(route);
	}

    ShutdownLua(&lua);
}

namespace { // anonymous namespace
	SimpleCreator<LuaPolicy> LuaPolicyCreator("lua");
}

}	// namespace Routing



/// LUA authentication policy

class LuaAuth : public LuaBase, public GkAuthenticator
{
public:
	enum SupportedRasChecks {
		/// bitmask of RAS checks implemented by this module
		LuaAuthRasChecks = RasInfo<H225_RegistrationRequest>::flag
//			| RasInfo<H225_UnregistrationRequest>::flag
//			| RasInfo<H225_BandwidthRequest>::flag
//			| RasInfo<H225_DisengageRequest>::flag
//			| RasInfo<H225_LocationRequest>::flag
//			| RasInfo<H225_InfoRequest>::flag
			| RasInfo<H225_AdmissionRequest>::flag,
		LuaAuthMiscChecks = e_Setup | e_SetupUnreg
	};

	LuaAuth(
		const char* name, /// a name for this module (a config section name)
		unsigned supportedRasChecks = LuaAuthRasChecks,
		unsigned supportedMiscChecks = LuaAuthMiscChecks
		);

	virtual ~LuaAuth();

	// overridden from class GkAuthenticator
//	virtual int Check(RasPDU<H225_UnregistrationRequest> & req, unsigned & rejectReason);
//	virtual int Check(RasPDU<H225_BandwidthRequest> & req, unsigned & rejectReason);
//	virtual int Check(RasPDU<H225_DisengageRequest> & req, unsigned & rejectReason);
//	virtual int Check(RasPDU<H225_LocationRequest> & req, unsigned & rejectReason);
//	virtual int Check(RasPDU<H225_InfoRequest> & req, unsigned & rejectReason);

	/** Authenticate/Authorize RAS or signaling message.
	    An override from GkAuthenticator.

	    @return
	    e_fail - authentication rejected the request
	    e_ok - authentication accepted the request
	    e_next - authentication is not supported for this request
	             or cannot be determined (SQL failure, no cryptoTokens, ...)
	*/
	virtual int Check(
		/// RRQ to be authenticated/authorized
		RasPDU<H225_RegistrationRequest> & request,
		/// authorization data (reject reason, ...)
		RRQAuthData & authData
		);
	virtual int Check(
		/// ARQ to be authenticated/authorized
		RasPDU<H225_AdmissionRequest> & request,
		/// authorization data (call duration limit, reject reason, ...)
		ARQAuthData & authData
		);

	/** Authenticate using data from Q.931 Setup message.

		@return:
		#GkAuthenticator::Status enum# with the result of authentication.
	*/
	virtual int Check(
		/// Q.931/H.225 Setup message to be authenticated
		SetupMsg & setup,
		/// authorization data (call duration limit, reject reason, ...)
		SetupAuthData & authData
		);

protected:
	/** Run the LUA registration authentication script

		@return
		e_ok 	if authentication OK
		e_fail	if authentication failed
		e_next	go to next policy
	*/
	int doRegistrationCheck(
		const PString & username,
        const PString & callingStationId,
		const PString & callerIP,
		const PString & aliases,
		const PString & vendor,
		const PString & messageType,
		const PString & message
		);

	/** Run the LUA call authentication script

		@return
		e_ok 	if authentication OK
		e_fail	if authentication failed
		e_next	go to next policy
	*/
	int doCallCheck(
		const PString & messageType,
		const PString & message,
		const PString & source,
		const PString & calledAlias,
		const PString & calledIP,
		const PString & caller,
		const PString & callingStationId,
		const PString & callid,
		const PString & srcinfo,
		const PString & vendor
		);

private:
	LuaAuth();
	LuaAuth(const LuaAuth &);
	LuaAuth & operator=(const LuaAuth &);

protected:
	// scripts to run
	PString m_registrationScript;
	PString m_callScript;
};

LuaAuth::LuaAuth(
	const char * name,
	unsigned supportedRasChecks,
	unsigned supportedMiscChecks)
	: GkAuthenticator(name, supportedRasChecks, supportedMiscChecks)
{
	m_registrationScript = GkConfig()->GetString("LuaAuth", "RegistrationScript", "");
	if (m_registrationScript.IsEmpty()) {
		PString scriptFile = GkConfig()->GetString("LuaAuth", "RegistrationScriptFile", "");
		if (!scriptFile.IsEmpty()) {
			PTextFile f(scriptFile, PFile::ReadOnly);
			if (!f.IsOpen()) {
				PTRACE(1, "LuaAuth\tCan't read LUA call script " << scriptFile);
			} else {
				PString line;
				while (f.ReadLine(line)) {
					m_registrationScript += (line + "\n");
				}
			}
		}
	}

	m_callScript = GkConfig()->GetString("LuaAuth", "CallScript", "");
	if (m_callScript.IsEmpty()) {
		PString scriptFile = GkConfig()->GetString("LuaAuth", "CallScriptFile", "");
		if (!scriptFile.IsEmpty()) {
			PTextFile f(scriptFile, PFile::ReadOnly);
			if (!f.IsOpen()) {
				PTRACE(1, "LuaAuth\tCan't read LUA call script " << scriptFile);
			} else {
				PString line;
				while (f.ReadLine(line)) {
					m_callScript += (line + "\n");
				}
			}
		}
	}

	if (m_registrationScript.IsEmpty() && m_callScript.IsEmpty()) {
		PTRACE(2, "LuaAuth\tno LUA script");
		SNMP_TRAP(4, SNMPError, General, "LuaAuth: no script");
		return;
	}
}

LuaAuth::~LuaAuth()
{
}

/*
int LuaAuth::Check(RasPDU<H225_UnregistrationRequest> & request, unsigned &)
{
	return doCheck(request);
}

int LuaAuth::Check(RasPDU<H225_BandwidthRequest> & request, unsigned &)
{
	return doCheck(request);
}

int LuaAuth::Check(RasPDU<H225_DisengageRequest> & request, unsigned &)
{
	return doCheck(request);
}

int LuaAuth::Check(RasPDU<H225_LocationRequest> & request, unsigned &)
{
	return doCheck(request);
}

int LuaAuth::Check(RasPDU<H225_InfoRequest> & request, unsigned &)
{
	return doCheck(request);
}
*/

int LuaAuth::Check(
	RasPDU<H225_RegistrationRequest> & rrqPdu,
	RRQAuthData & authData)
{
	H225_RegistrationRequest & rrq = rrqPdu;

	PString username = GetUsername(rrqPdu);
	PString callingStationId = GetCallingStationId(rrqPdu, authData);
	PIPSocket::Address addr = (rrqPdu.operator->())->m_peerAddr;
	PString callerIP = addr.AsString();
    PString aliases = "";
	if (rrq.HasOptionalField(H225_RegistrationRequest::e_terminalAlias)) {
		for (PINDEX i = 0; i < rrq.m_terminalAlias.GetSize(); i++) {
			if (i > 0) {
				aliases += ",";
            }
			aliases += AsString(rrq.m_terminalAlias[i], FALSE);
		}
	}
    PString vendor = "";
    if (rrq.m_endpointVendor.HasOptionalField(H225_EndpointType::e_vendor)) {
        if (rrq.m_endpointVendor.HasOptionalField(H225_VendorIdentifier::e_productId)) {
            vendor += rrq.m_endpointVendor.m_productId.AsString();
        }
        if (rrq.m_endpointVendor.HasOptionalField(H225_VendorIdentifier::e_versionId)) {
            vendor += rrq.m_endpointVendor.m_versionId.AsString();
        }
    }

	PString messageType = "RRQ";
    PStringStream strm;
    rrq.PrintOn(strm);
    PString message = strm;

	return doRegistrationCheck(username, callingStationId, callerIP, aliases, vendor, messageType, message);
}

int LuaAuth::Check(
	/// ARQ to be authenticated/authorized
	RasPDU<H225_AdmissionRequest> & request,
	/// authorization data (call duration limit, reject reason, ...)
	ARQAuthData & authData)
{
	H225_ArrayOf_AliasAddress aliases;
	H225_AdmissionRequest & arq = request;
	if (arq.HasOptionalField(H225_AdmissionRequest::e_destinationInfo)) {
		aliases = arq.m_destinationInfo;
	}
	endptr ep = RegistrationTable::Instance()->FindByEndpointId(arq.m_endpointIdentifier);
	if (ep) {
		PString messageType = "ARQ";
        PStringStream strm;
        arq.PrintOn(strm);
        PString message = strm;
		PString source = AsDotString(ep->GetCallSignalAddress());
		PString calledAlias = "";
		if (aliases.GetSize() > 0)
			calledAlias = AsString(aliases[0], FALSE);
		PString calledIP = "";	/* not available for ARQs */
		PString caller = AsString(arq.m_srcInfo, FALSE);
		PString callingStationId = authData.m_callingStationId;
		PString callid = AsString(arq.m_callIdentifier);
		PString srcinfo = AsString(arq.m_srcInfo, false);
		PString vendor;
        if (ep->GetEndpointType().HasOptionalField(H225_EndpointType::e_vendor)) {
			if (ep->GetEndpointType().m_vendor.HasOptionalField(H225_VendorIdentifier::e_productId)) {
				vendor += ep->GetEndpointType().m_vendor.m_productId.AsString();
			}
			if (ep->GetEndpointType().m_vendor.HasOptionalField(H225_VendorIdentifier::e_versionId)) {
				vendor += ep->GetEndpointType().m_vendor.m_versionId.AsString();
			}
        }

		return doCallCheck(messageType, message, source, calledAlias, calledIP, caller, callingStationId, callid, srcinfo, vendor);
	} else {
		return e_fail;
	}
}

int LuaAuth::Check(
	/// Q.931/H.225 Setup message to be authenticated
	SetupMsg & setup,
	/// authorization data (call duration limit, reject reason, ...)
	SetupAuthData & authData
	)
{
	H225_Setup_UUIE & setup_uuie = setup.GetUUIEBody();

	PString messageType = "Setup";
    PStringStream strm;
    setup_uuie.PrintOn(strm);
    PString message = strm;
	PString source = "";
	if (setup_uuie.HasOptionalField(H225_Setup_UUIE::e_sourceCallSignalAddress) && setup_uuie.m_sourceCallSignalAddress.IsValid())
		source = AsDotString(setup_uuie.m_sourceCallSignalAddress);
	PString calledAlias = GetCalledStationId(setup, authData);
	PString calledIP = "";
	if (setup_uuie.HasOptionalField(H225_Setup_UUIE::e_destCallSignalAddress))
		calledIP = AsDotString(setup_uuie.m_destCallSignalAddress);
	PString callingStationId = GetCallingStationId(setup, authData);
	PString caller = callingStationId;
	PString callid = AsString(setup_uuie.m_callIdentifier);
	PString srcinfo;
	if (setup.GetUUIEBody().HasOptionalField(H225_Setup_UUIE::e_sourceAddress)
        && setup.GetUUIEBody().m_sourceAddress.GetSize() > 0) {
        srcinfo = AsString(setup.GetUUIEBody().m_sourceAddress, false);
	}
	PString vendor;
    if (setup.GetUUIEBody().m_sourceInfo.HasOptionalField(H225_EndpointType::e_vendor)) {
        if (setup.GetUUIEBody().m_sourceInfo.m_vendor.HasOptionalField(H225_VendorIdentifier::e_productId)) {
            vendor += setup.GetUUIEBody().m_sourceInfo.m_vendor.m_productId.AsString();
        }
        if (setup.GetUUIEBody().m_sourceInfo.m_vendor.HasOptionalField(H225_VendorIdentifier::e_versionId)) {
            vendor += setup.GetUUIEBody().m_sourceInfo.m_vendor.m_versionId.AsString();
        }
    }

	return doCallCheck(messageType, message, source, calledAlias, calledIP, caller, callingStationId, callid, srcinfo, vendor);
}

int LuaAuth::doRegistrationCheck(
		const PString & username,
		const PString & callingStationId,
		const PString & callerIP,
		const PString & aliases,
		const PString & vendor,
		const PString & messageType,
		const PString & message
		)
{
	lua_State * lua = NULL;
    InitLua(&lua);

    if (!lua || m_registrationScript.IsEmpty()) {
		PTRACE(1, "LuaAuth\tError: LUA not configured");
        ShutdownLua(&lua);
		return e_fail;
    }

	SetString(lua, "username", username);
	SetString(lua, "callingStationId", callingStationId);
	SetString(lua, "callerIP", callerIP);
	SetString(lua, "aliases", aliases);
	SetString(lua, "vendor", vendor);
	SetString(lua, "messageType", messageType);
	SetString(lua, "message", message);
	SetString(lua, "result", "FAIL");

	if (!RunLua(lua, m_registrationScript)) {
        ShutdownLua(&lua);
		return e_fail;
	}

	PString result = GetString(lua, "result");
	int resultCode = e_fail;
	if (result.ToUpper() == "OK") {
		resultCode = e_ok;
	} else if (result.ToUpper() == "NEXT") {
		resultCode = e_next;
	}

    ShutdownLua(&lua);
    return resultCode;
}

int LuaAuth::doCallCheck(
		const PString & messageType,
		const PString & message,
		const PString & source,
		const PString & calledAlias,
		const PString & calledIP,
		const PString & caller,
		const PString & callingStationId,
		const PString & callid,
		const PString & srcinfo,
		const PString & vendor
		)
{
	lua_State * lua = NULL;
    InitLua(&lua);

    if (!lua || m_callScript.IsEmpty()) {
		PTRACE(1, "LuaAuth\tError: LUA not configured");
        ShutdownLua(&lua);
		return e_fail;
    }

	SetString(lua, "messageType", messageType);
	SetString(lua, "message", message);
	SetString(lua, "source", source);
	SetString(lua, "calledAlias", calledAlias);
	SetString(lua, "calledIP", calledIP);
	SetString(lua, "caller", caller);
	SetString(lua, "callingStationId", callingStationId);
	SetString(lua, "callid", callid);
	SetString(lua, "srcInfo", srcinfo);
	SetString(lua, "vendor", vendor);
	SetString(lua, "result", "FAIL");

	if (!RunLua(lua, m_callScript)) {
        ShutdownLua(&lua);
		return e_fail;
	}

	PString result = GetString(lua, "result");
	int resultCode = e_fail;
	if (result.ToUpper() == "OK") {
		resultCode = e_ok;
	} else if (result.ToUpper() == "NEXT") {
		resultCode = e_next;
	}

    ShutdownLua(&lua);
    return resultCode;
}

namespace { // anonymous namespace
	GkAuthCreator<LuaAuth> LuaAuthCreator("LuaAuth");
} // end of anonymous namespace


class LuaPasswordAuth : public LuaBase, public SimplePasswordAuth
{
public:
	/// build authenticator reading settings from the config
	LuaPasswordAuth(
		/// name for this authenticator and for the config section to read settings from
		const char* authName
		);

	virtual ~LuaPasswordAuth();

protected:
	/** Override from SimplePasswordAuth.

	    @return
	    True if the password has been found for the given alias.
	*/
	virtual bool GetPassword(
		/// alias to check the password for
		const PString & alias,
		/// password string, if the match is found
		PString & password,
        /// map of authentication parameters
		std::map<PString, PString> & params);

private:
	LuaPasswordAuth();
	LuaPasswordAuth(const LuaPasswordAuth &);
	LuaPasswordAuth & operator=(const LuaPasswordAuth &);

protected:
	// script to run
	PString m_script;
};

LuaPasswordAuth::LuaPasswordAuth(const char* authName)
	: SimplePasswordAuth(authName)
{
	m_script = GkConfig()->GetString("LuaPasswordAuth", "Script", "");
	if (m_script.IsEmpty()) {
		PString scriptFile = GkConfig()->GetString("LuaPasswordAuth", "ScriptFile", "");
		if (!scriptFile.IsEmpty()) {
			PTextFile f(scriptFile, PFile::ReadOnly);
			if (!f.IsOpen()) {
				PTRACE(1, "LuaPasswordAuth\tCan't read LUA call script " << scriptFile);
			} else {
				PString line;
				while (f.ReadLine(line)) {
					m_script += (line + "\n");
				}
			}
		}
	}

	if (m_script.IsEmpty()) {
		PTRACE(2, "LuaPasswordAuth\tno LUA script");
		SNMP_TRAP(4, SNMPError, General, "LuaPasswordAuth: no script");
		return;
	}
}

LuaPasswordAuth::~LuaPasswordAuth()
{
}

bool LuaPasswordAuth::GetPassword(const PString & alias, PString & password, std::map<PString, PString> & params)
{
	lua_State * lua = NULL;
    InitLua(&lua);

    if (!lua || m_script.IsEmpty()) {
		PTRACE(1, "LuaPasswordAuth\tError: LUA not configured");
        ShutdownLua(&lua);
		return false;
    }

	SetString(lua, "alias", alias);
	SetString(lua, "gk", Toolkit::GKName());
	SetString(lua, "password", "");
	// TODO: add other parameters from param

	if (!RunLua(lua, m_script)) {
        ShutdownLua(&lua);
		return false;
	}

	password = GetString(lua, "password");
    ShutdownLua(&lua);
	return true;
}

namespace { // anonymous namespace
	GkAuthCreator<LuaPasswordAuth> LuaPasswordAuthCreator("LuaPasswordAuth");
} // end of anonymous namespace


class LuaAcct : public LuaBase, public GkAcctLogger
{
public:
	enum Constants
	{
		/// events recognized by this module
		StatusAcctEvents = AcctOn | AcctOff | AcctStart | AcctStop | AcctUpdate | AcctConnect | AcctAlert | AcctRegister | AcctUnregister
	};

	LuaAcct(
		/// name from Gatekeeper::Acct section
		const char* moduleName,
		/// config section name to be used with an instance of this module,
		/// pass NULL to use a default section (named "moduleName")
		const char* cfgSecName = NULL
		);

	/// Destroy the accounting logger
	virtual ~LuaAcct();

	/// overridden from GkAcctLogger
	virtual Status Log(AcctEvent evt, const callptr & call);

	/// overridden from GkAcctLogger
	virtual Status Log(AcctEvent evt, const endptr & ep);

private:
	LuaAcct();
	/* No copy constructor allowed */
	LuaAcct(const LuaAcct &);
	/* No operator= allowed */
	LuaAcct & operator=(const LuaAcct &);

protected:
	/// script to run
	PString m_script;
	/// timestamp formatting string
	PString m_timestampFormat;
};

LuaAcct::LuaAcct(const char* moduleName, const char* cfgSecName)
	: GkAcctLogger(moduleName, cfgSecName)
{
	// it is very important to set what type of accounting events
	// are supported for each accounting module, otherwise the Log method
	// will no get called
	SetSupportedEvents(StatusAcctEvents);

	m_timestampFormat = GkConfig()->GetString("LuaAcct", "TimestampFormat", "");
	m_script = GkConfig()->GetString("LuaAcct", "Script", "");
	if (m_script.IsEmpty()) {
		PString scriptFile = GkConfig()->GetString("LuaAcct", "ScriptFile", "");
		if (!scriptFile.IsEmpty()) {
			PTextFile f(scriptFile, PFile::ReadOnly);
			if (!f.IsOpen()) {
				PTRACE(1, "LUA\tCan't read LUA script " << scriptFile);
			} else {
				PString line;
				while (f.ReadLine(line)) {
					m_script += (line + "\n");
				}
			}
		}
	}

	if (m_script.IsEmpty()) {
		PTRACE(2, GetName() << "\tmodule creation failed: no LUA script");
		SNMP_TRAP(4, SNMPError, General, GetName() + " creation failed");
		return;
	}
}

LuaAcct::~LuaAcct()
{
}

GkAcctLogger::Status LuaAcct::Log(GkAcctLogger::AcctEvent evt, const callptr & call)
{
	// a workaround to prevent processing end on "sufficient" module
	// if it is not interested in this event type
	if ((evt & GetEnabledEvents() & GetSupportedEvents()) == 0)
		return Next;

	lua_State * lua = NULL;
    InitLua(&lua);

    if (!lua || m_script.IsEmpty()) {
		PTRACE(1, GetName() + "\tError: LUA not configured");
        ShutdownLua(&lua);
		return Fail;
    }

	if (!call && evt != AcctOn && evt != AcctOff) {
		PTRACE(1, GetName() << "\tMissing call info for event " << evt);
        ShutdownLua(&lua);
		return Fail;
	}

	PString eventName;
	switch(evt) {
        case AcctOn:
            eventName = "On";
            break;
        case AcctOff:
            eventName = "Off";
            break;
        case AcctStart:
            eventName = "Start";
            break;
        case AcctConnect:
            eventName = "Connect";
            break;
        case AcctUpdate:
            eventName = "Update";
            break;
        case AcctStop:
            eventName = "Stop";
            break;
        case AcctAlert:
            eventName = "Alert";
            break;
        default:
            eventName = "Unknown";
	}

	std::map<PString, PString> params;
	if (evt == AcctOn || evt == AcctOff) {
		SetupAcctParams(params);
    } else {
        SetupAcctParams(params, call, m_timestampFormat);
    }

	SetString(lua, "event", eventName);
	SetString(lua, "result", "OK");
	if (!lua_checkstack(lua, params.size())) {
        PTRACE(1, "LuaAcct\tError: Not enough room on stack");
        ShutdownLua(&lua);
        return Fail;
	}
    for(std::map<PString, PString>::const_iterator it = params.begin(); it != params.end(); ++it) {
        PString varName = PString("param_") + it->first;
        varName.Replace("-", "_", true);    // - not allowed in LUA variable name
        SetString(lua, varName, PString(it->second));
    }

	if (!RunLua(lua, m_script)) {
        ShutdownLua(&lua);
		return Fail;
	}

	PString result = GetString(lua, "result");
	GkAcctLogger::Status resultCode = Fail;
	if (result.ToUpper() == "OK") {
		resultCode = Ok;
	} else if (result.ToUpper() == "NEXT") {
		resultCode = Next;
	}

    ShutdownLua(&lua);
	return resultCode;
}

GkAcctLogger::Status LuaAcct::Log(GkAcctLogger::AcctEvent evt, const endptr & ep)
{
	// a workaround to prevent processing end on "sufficient" module
	// if it is not interested in this event type
	if ((evt & GetEnabledEvents() & GetSupportedEvents()) == 0)
		return Next;

	lua_State * lua = NULL;
    InitLua(&lua);

    if (!lua || m_script.IsEmpty()) {
		PTRACE(1, GetName() + "\tError: LUA not configured");
        ShutdownLua(&lua);
		return Fail;
    }

	if (!ep) {
		PTRACE(1, GetName() << "\tMissing endpoint info for event " << evt);
        ShutdownLua(&lua);
		return Fail;
	}

	PString eventName;
	switch(evt) {
        case AcctRegister:
            eventName = "Register";
            break;
        case AcctUnregister:
            eventName = "Unregister";
            break;
        default:
            eventName = "Unknown";
	}

	std::map<PString, PString> params;
	SetupAcctEndpointParams(params, ep, m_timestampFormat);

	SetString(lua, "event", eventName);
	SetString(lua, "result", "OK");
	if (!lua_checkstack(lua, params.size())) {
        PTRACE(1, "LuaAcct\tError: Not enough room on stack");
        ShutdownLua(&lua);
        return Fail;
	}
    for(std::map<PString, PString>::const_iterator it = params.begin(); it != params.end(); ++it) {
        PString varName = PString("param_") + it->first;
        varName.Replace("-", "_", true);    // - not allowed in LUA variable name
        SetString(lua, varName, PString(it->second));
    }

	if (!RunLua(lua, m_script)) {
        ShutdownLua(&lua);
		return Fail;
	}

	PString result = GetString(lua, "result");
	GkAcctLogger::Status resultCode = Fail;
	if (result.ToUpper() == "OK") {
		resultCode = Ok;
	} else if (result.ToUpper() == "NEXT") {
		resultCode = Next;
	}

    ShutdownLua(&lua);
	return resultCode;
}


namespace {
	// append status port accounting logger to the global list of loggers
	GkAcctLoggerCreator<LuaAcct> LuaAcctCreator("LuaAcct");
}

#endif	// HAS_LUA
