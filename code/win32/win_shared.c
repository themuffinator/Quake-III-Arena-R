/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include <lmerr.h>
#include <lmcons.h>
#include <lmwksta.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

/*
================
Sys_Milliseconds
================
*/
int			sys_timeBase;
int Sys_Milliseconds (void)
{
	int			sys_curtime;
	static qboolean	initialized = qfalse;

	if (!initialized) {
		sys_timeBase = timeGetTime();
		initialized = qtrue;
	}
	sys_curtime = timeGetTime() - sys_timeBase;

	return sys_curtime;
}

void Sys_SnapVector( float *v )
{
	v[0] = (int)v[0];
	v[1] = (int)v[1];
	v[2] = (int)v[2];
}


/*
**
** Disable all optimizations temporarily so this code works correctly!
**
*/
#pragma optimize( "", off )

/*
** --------------------------------------------------------------------------------
**
** PROCESSOR STUFF
**
** --------------------------------------------------------------------------------
*/

int Sys_GetProcessorId( void )
{
	return CPUID_GENERIC;
}

/*
**
** Re-enable optimizations back to what they were
**
*/
#pragma optimize( "", on )

//============================================

char *Sys_GetCurrentUser( void )
{
	static char s_userName[1024];
	unsigned long size = sizeof( s_userName );


	if ( !GetUserName( s_userName, &size ) )
		strcpy( s_userName, "player" );

	if ( !s_userName[0] )
	{
		strcpy( s_userName, "player" );
	}

	return s_userName;
}

char	*Sys_DefaultHomePath(void) {
	return NULL;
}

static qboolean Sys_FileReadable(const char* path) {
	FILE* f;

	if (!path || !path[0]) {
		return qfalse;
	}

	f = fopen(path, "rb");
	if (!f) {
		return qfalse;
	}

	fclose(f);
	return qtrue;
}

static void Sys_NormalizePathSlashes(char* path) {
	char* p;

	if (!path) {
		return;
	}

	for (p = path; *p; ++p) {
		if (*p == '/') {
			*p = '\\';
		}
	}
}

static void Sys_TrimPathQuotes(char* path) {
	size_t len;

	if (!path || !path[0]) {
		return;
	}

	len = strlen(path);
	if (len >= 2 && path[0] == '"' && path[len - 1] == '"') {
		memmove(path, path + 1, len - 2);
		path[len - 2] = '\0';
	}
}

static qboolean Sys_PathHasQuake3Assets(const char* basePath) {
	char pak0Path[MAX_OSPATH];
	char defaultCfgPath[MAX_OSPATH];

	if (!basePath || !basePath[0]) {
		return qfalse;
	}

	Com_sprintf(pak0Path, sizeof(pak0Path), "%s\\baseq3\\pak0.pk3", basePath);
	if (Sys_FileReadable(pak0Path)) {
		return qtrue;
	}

	Com_sprintf(defaultCfgPath, sizeof(defaultCfgPath), "%s\\baseq3\\default.cfg", basePath);
	if (Sys_FileReadable(defaultCfgPath)) {
		return qtrue;
	}

	return qfalse;
}

static qboolean Sys_OpenRegistryKeyAnyView(HKEY root, const char* subKey, HKEY* outKey) {
	const REGSAM samDesired[] = { KEY_READ | KEY_WOW64_64KEY, KEY_READ | KEY_WOW64_32KEY, KEY_READ };
	int i;

	for (i = 0; i < (int)(sizeof(samDesired) / sizeof(samDesired[0])); ++i) {
		if (RegOpenKeyExA(root, subKey, 0, samDesired[i], outKey) == ERROR_SUCCESS) {
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean Sys_ReadRegistryString(HKEY root, const char* subKey, const char* valueName, char* outValue, int outValueSize) {
	HKEY key;
	DWORD type;
	DWORD dataSize;
	char expanded[MAX_OSPATH];

	if (!outValue || outValueSize <= 0) {
		return qfalse;
	}

	outValue[0] = '\0';

	if (!Sys_OpenRegistryKeyAnyView(root, subKey, &key)) {
		return qfalse;
	}

	type = 0;
	dataSize = (DWORD)outValueSize;
	if (RegQueryValueExA(key, valueName, NULL, &type, (LPBYTE)outValue, &dataSize) != ERROR_SUCCESS) {
		RegCloseKey(key);
		return qfalse;
	}
	RegCloseKey(key);

	outValue[outValueSize - 1] = '\0';

	if (type != REG_SZ && type != REG_EXPAND_SZ) {
		return qfalse;
	}

	if (type == REG_EXPAND_SZ) {
		if (ExpandEnvironmentStringsA(outValue, expanded, sizeof(expanded)) > 0) {
			Q_strncpyz(outValue, expanded, outValueSize);
		}
	}

	Sys_TrimPathQuotes(outValue);
	Sys_NormalizePathSlashes(outValue);
	return qtrue;
}

static qboolean Sys_TryInstallPathCandidate(const char* candidatePath, char* outPath, int outPathSize) {
	char normalized[MAX_OSPATH];

	if (!candidatePath || !candidatePath[0]) {
		return qfalse;
	}

	Q_strncpyz(normalized, candidatePath, sizeof(normalized));
	Sys_TrimPathQuotes(normalized);
	Sys_NormalizePathSlashes(normalized);

	if (!normalized[0]) {
		return qfalse;
	}

	if (Sys_PathHasQuake3Assets(normalized)) {
		Q_strncpyz(outPath, normalized, outPathSize);
		return qtrue;
	}

	return qfalse;
}

static qboolean Sys_TrySteamLibraryRoot(const char* steamRoot, char* outPath, int outPathSize) {
	char candidate[MAX_OSPATH];

	if (!steamRoot || !steamRoot[0]) {
		return qfalse;
	}

	Com_sprintf(candidate, sizeof(candidate), "%s\\steamapps\\common\\Quake 3 Arena", steamRoot);
	if (Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	Com_sprintf(candidate, sizeof(candidate), "%s\\steamapps\\common\\Quake III Arena", steamRoot);
	if (Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	Com_sprintf(candidate, sizeof(candidate), "%s\\steamapps\\common\\Quake III Gold", steamRoot);
	if (Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	return qfalse;
}

static void Sys_UnescapeSteamPath(const char* input, char* output, int outputSize) {
	int outIdx;
	const char* in;

	if (!output || outputSize <= 0) {
		return;
	}

	outIdx = 0;
	in = input;
	while (in && *in && outIdx < outputSize - 1) {
		if (in[0] == '\\' && in[1] == '\\') {
			output[outIdx++] = '\\';
			in += 2;
			continue;
		}
		output[outIdx++] = *in++;
	}
	output[outIdx] = '\0';
}

static qboolean Sys_TrySteamLibraryFoldersVdf(const char* steamRoot, char* outPath, int outPathSize) {
	char vdfPath[MAX_OSPATH];
	char line[4096];
	char escapedPath[MAX_OSPATH];
	char unescapedPath[MAX_OSPATH];
	FILE* file;

	if (!steamRoot || !steamRoot[0]) {
		return qfalse;
	}

	Com_sprintf(vdfPath, sizeof(vdfPath), "%s\\steamapps\\libraryfolders.vdf", steamRoot);
	file = fopen(vdfPath, "r");
	if (!file) {
		return qfalse;
	}

	while (fgets(line, sizeof(line), file)) {
		char* pathToken;
		char* valueBegin;
		char* valueEnd;
		int valueLength;

		pathToken = strstr(line, "\"path\"");
		if (!pathToken) {
			continue;
		}

		valueBegin = strchr(pathToken + 6, '"');
		if (!valueBegin) {
			continue;
		}

		valueEnd = strchr(valueBegin + 1, '"');
		if (!valueEnd) {
			continue;
		}

		valueLength = (int)(valueEnd - (valueBegin + 1));
		if (valueLength <= 0 || valueLength >= (int)sizeof(escapedPath)) {
			continue;
		}

		memcpy(escapedPath, valueBegin + 1, valueLength);
		escapedPath[valueLength] = '\0';
		Sys_UnescapeSteamPath(escapedPath, unescapedPath, sizeof(unescapedPath));
		if (Sys_TrySteamLibraryRoot(unescapedPath, outPath, outPathSize)) {
			fclose(file);
			return qtrue;
		}
	}

	fclose(file);
	return qfalse;
}

static qboolean Sys_FindSteamInstallPath(char* outPath, int outPathSize) {
	char candidate[MAX_OSPATH];

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 2200", "InstallLocation", candidate, sizeof(candidate)) &&
		Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_ReadRegistryString(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 2200", "InstallLocation", candidate, sizeof(candidate)) &&
		Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_ReadRegistryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", candidate, sizeof(candidate))) {
		if (Sys_TrySteamLibraryRoot(candidate, outPath, outPathSize)) {
			return qtrue;
		}
		if (Sys_TrySteamLibraryFoldersVdf(candidate, outPath, outPathSize)) {
			return qtrue;
		}
	}

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath", candidate, sizeof(candidate))) {
		if (Sys_TrySteamLibraryRoot(candidate, outPath, outPathSize)) {
			return qtrue;
		}
		if (Sys_TrySteamLibraryFoldersVdf(candidate, outPath, outPathSize)) {
			return qtrue;
		}
	}

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "SteamPath", candidate, sizeof(candidate))) {
		if (Sys_TrySteamLibraryRoot(candidate, outPath, outPathSize)) {
			return qtrue;
		}
		if (Sys_TrySteamLibraryFoldersVdf(candidate, outPath, outPathSize)) {
			return qtrue;
		}
	}

	if (Sys_TrySteamLibraryRoot("C:\\Program Files (x86)\\Steam", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_TrySteamLibraryRoot("C:\\Program Files\\Steam", outPath, outPathSize)) {
		return qtrue;
	}

	return qfalse;
}

static qboolean Sys_FindGogInstallPathFromGamesRegistry(HKEY root, const char* gamesSubKey, char* outPath, int outPathSize) {
	HKEY gamesKey;
	DWORD index;
	char gameSubKeyName[256];
	char fullSubKey[MAX_OSPATH];
	char candidate[MAX_OSPATH];

	if (!Sys_OpenRegistryKeyAnyView(root, gamesSubKey, &gamesKey)) {
		return qfalse;
	}

	index = 0;
	while (1) {
		DWORD gameSubKeyNameLen = sizeof(gameSubKeyName);
		LONG result = RegEnumKeyExA(gamesKey, index, gameSubKeyName, &gameSubKeyNameLen, NULL, NULL, NULL, NULL);
		if (result != ERROR_SUCCESS) {
			break;
		}

		Com_sprintf(fullSubKey, sizeof(fullSubKey), "%s\\%s", gamesSubKey, gameSubKeyName);
		if ((Sys_ReadRegistryString(root, fullSubKey, "path", candidate, sizeof(candidate)) ||
			 Sys_ReadRegistryString(root, fullSubKey, "PATH", candidate, sizeof(candidate))) &&
			Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
			RegCloseKey(gamesKey);
			return qtrue;
		}

		index++;
	}

	RegCloseKey(gamesKey);
	return qfalse;
}

static qboolean Sys_FindGogInstallPath(char* outPath, int outPathSize) {
	char candidate[MAX_OSPATH];

	if (Sys_FindGogInstallPathFromGamesRegistry(HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_FindGogInstallPathFromGamesRegistry(HKEY_CURRENT_USER, "SOFTWARE\\GOG.com\\Games", outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GOGPACKQUAKEIIIARENA_is1", "InstallLocation", candidate, sizeof(candidate)) &&
		Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GOGPACKQUAKE3ARENA_is1", "InstallLocation", candidate, sizeof(candidate)) &&
		Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_TryInstallPathCandidate("C:\\GOG Games\\Quake III", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_TryInstallPathCandidate("C:\\GOG Games\\Quake III Arena", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_TryInstallPathCandidate("C:\\Program Files (x86)\\GOG Galaxy\\Games\\Quake III", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_TryInstallPathCandidate("C:\\Program Files\\GOG Galaxy\\Games\\Quake III", outPath, outPathSize)) {
		return qtrue;
	}

	return qfalse;
}

static qboolean Sys_FindCdInstallPath(char* outPath, int outPathSize) {
	char candidate[MAX_OSPATH];

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\id\\Quake III Arena", "InstallPath", candidate, sizeof(candidate)) &&
		Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\id software\\Quake III Arena", "InstallPath", candidate, sizeof(candidate)) &&
		Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Quake III Arena", "InstallLocation", candidate, sizeof(candidate)) &&
		Sys_TryInstallPathCandidate(candidate, outPath, outPathSize)) {
		return qtrue;
	}

	if (Sys_TryInstallPathCandidate("C:\\Quake3", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_TryInstallPathCandidate("C:\\Quake III Arena", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_TryInstallPathCandidate("C:\\Program Files (x86)\\Quake III Arena", outPath, outPathSize)) {
		return qtrue;
	}
	if (Sys_TryInstallPathCandidate("C:\\Program Files\\Quake III Arena", outPath, outPathSize)) {
		return qtrue;
	}

	return qfalse;
}

char *Sys_DefaultInstallPath(void) {
	static qboolean initialized = qfalse;
	static char detectedPath[MAX_OSPATH];
	char cwd[MAX_OSPATH];

	if (initialized) {
		return detectedPath;
	}
	initialized = qtrue;

	Q_strncpyz(cwd, Sys_Cwd(), sizeof(cwd));
	Sys_NormalizePathSlashes(cwd);

	// Priority 1: local assets in the current directory.
	if (Sys_PathHasQuake3Assets(cwd)) {
		Q_strncpyz(detectedPath, cwd, sizeof(detectedPath));
		Com_Printf("Install detection: using current directory assets at '%s'\n", detectedPath);
		return detectedPath;
	}

	// Priority 2: Steam install.
	if (Sys_FindSteamInstallPath(detectedPath, sizeof(detectedPath))) {
		Com_Printf("Install detection: using Steam install at '%s'\n", detectedPath);
		return detectedPath;
	}

	// Priority 3: GOG install.
	if (Sys_FindGogInstallPath(detectedPath, sizeof(detectedPath))) {
		Com_Printf("Install detection: using GOG install at '%s'\n", detectedPath);
		return detectedPath;
	}

	// Priority 4: traditional/CD-era install.
	if (Sys_FindCdInstallPath(detectedPath, sizeof(detectedPath))) {
		Com_Printf("Install detection: using CD-era install at '%s'\n", detectedPath);
		return detectedPath;
	}

	// Final fallback: keep legacy behavior.
	Q_strncpyz(detectedPath, cwd, sizeof(detectedPath));
	Com_Printf("Install detection: no install found, falling back to '%s'\n", detectedPath);
	return detectedPath;
}

