/*-------------------------------------------------------------------------
 *
 * security.c
 *    Microsoft Windows Win32 Security Support Functions
 *
 * Portions Copyright (c) 1996-2003, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/port/win32/security.c,v 1.1 2004/06/24 21:02:42 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"


/*
 * Returns nonzero if the current user has administrative privileges,
 * or zero if not.
 *
 * Note: this cannot use ereport() because it's called too early during
 * startup.
 */
int
pgwin32_is_admin(void)
{
	HANDLE AccessToken;
	UCHAR InfoBuffer[1024];
	PTOKEN_GROUPS Groups = (PTOKEN_GROUPS)InfoBuffer; 
	DWORD InfoBufferSize;
	PSID AdministratorsSid;
	PSID PowerUsersSid;
	SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY }; 
	UINT x;
	BOOL success;
	
	if(!OpenProcessToken(GetCurrentProcess(),TOKEN_READ,&AccessToken))
	{
		write_stderr("failed to open process token: %d\n",
					 (int)GetLastError());
		exit(1);
	}

	if (!GetTokenInformation(AccessToken,TokenGroups,InfoBuffer,
							 1024, &InfoBufferSize))
	{
		write_stderr("failed to get token information: %d\n",
					 (int)GetLastError());
		exit(1);
	}

	CloseHandle(AccessToken);

	if(!AllocateAndInitializeSid(&NtAuthority, 2,
								 SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
								 0,&AdministratorsSid))
	{
		write_stderr("failed to get SID for Administrators group: %d\n",
					 (int)GetLastError());
		exit(1);
	}

	if (!AllocateAndInitializeSid(&NtAuthority, 2,
								  SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_POWER_USERS, 0, 0, 0, 0, 0,
								  0, &PowerUsersSid))
	{
		write_stderr("failed to get SID for PowerUsers group: %d\n",
					 (int)GetLastError());
		exit(1);
	}
	
	success = FALSE;
	
	for (x=0; x<Groups->GroupCount; x++)
	{
		if (EqualSid(AdministratorsSid, Groups->Groups[x].Sid) ||
			EqualSid(PowerUsersSid, Groups->Groups[x].Sid))
		{
			success = TRUE;
			break;
		}
	}
	
	FreeSid(AdministratorsSid);
	FreeSid(PowerUsersSid);
	return success;
}

/*
 * We consider ourselves running as a service if one of the following is
 * true:
 *
 * 1) We are running as Local System (only used by services)
 * 2) Our token contains SECURITY_SERVICE_RID (automatically added to the
 *    process token by the SCM when starting a service)
 *
 * Return values:
 *   0 = Not service
 *   1 = Service
 *  -1 = Error
 *
 * Note: we can't report errors via either ereport (we're called too early)
 * or write_stderr (because that calls this).  We are therefore reduced to
 * writing directly on stderr, which sucks, but we have few alternatives.
 */
int
pgwin32_is_service(void)
{
	static int _is_service = -1;
	HANDLE AccessToken;
	UCHAR InfoBuffer[1024];
	PTOKEN_GROUPS Groups = (PTOKEN_GROUPS)InfoBuffer;
	PTOKEN_USER User = (PTOKEN_USER)InfoBuffer;
	DWORD InfoBufferSize;
	PSID ServiceSid;
	PSID LocalSystemSid;
	SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY }; 
	UINT x;

	/* Only check the first time */
	if (_is_service != -1)
		return _is_service;
	
	if (!OpenProcessToken(GetCurrentProcess(),TOKEN_READ,&AccessToken)) {
		fprintf(stderr,"failed to open process token: %d\n",
				(int)GetLastError());
		return -1;
	}

	/* First check for local system */
	if (!GetTokenInformation(AccessToken,TokenUser,InfoBuffer,1024,&InfoBufferSize)) {
		fprintf(stderr,"failed to get token information: %d\n",
				(int)GetLastError());
		return -1;
	}
	
	if (!AllocateAndInitializeSid(&NtAuthority,1,
								  SECURITY_LOCAL_SYSTEM_RID,0,0,0,0,0,0,0,
								  &LocalSystemSid)) {
		fprintf(stderr,"failed to get SID for local system account\n");
		CloseHandle(AccessToken);
		return -1;
	}

	if (EqualSid(LocalSystemSid, User->User.Sid)) {
		FreeSid(LocalSystemSid);
		CloseHandle(AccessToken);
		_is_service = 1;
		return _is_service;
	}

	FreeSid(LocalSystemSid);

	/* Now check for group SID */
	if (!GetTokenInformation(AccessToken,TokenGroups,InfoBuffer,1024,&InfoBufferSize)) {
		fprintf(stderr,"failed to get token information: %d\n",
				(int)GetLastError());
		return -1;
	}

	if (!AllocateAndInitializeSid(&NtAuthority,1,
								  SECURITY_SERVICE_RID, 0, 0, 0, 0, 0, 0, 0,
								  &ServiceSid)) {
		fprintf(stderr,"failed to get SID for service group\n");
		CloseHandle(AccessToken);
		return -1;
	}

	_is_service = 0;
	for (x = 0; x < Groups->GroupCount; x++)
	{
		if (EqualSid(ServiceSid, Groups->Groups[x].Sid)) 
		{
			_is_service = 1;
			break;
		}
	}

	FreeSid(ServiceSid);

	CloseHandle(AccessToken);

	return _is_service;
}