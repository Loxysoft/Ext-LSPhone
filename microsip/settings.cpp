/*
 * Copyright (C) 2011-2018 MicroSIP (http://www.microsip.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "stdafx.h"
#include "settings.h"
#include "Crypto.h"

#include <algorithm>
#include <vector>

#include <Msi.h>
#pragma comment(lib, "Msi.lib")

using namespace MFC;

AccountSettings accountSettings;
bool firstRun;
bool pj_ready;
CTime startTime;
CArray<Shortcut, Shortcut> shortcuts;

static LONGLONG FileSize(const wchar_t* name)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(name, GetFileExInfoStandard, &fad))
		return -1; // error condition, could call GetLastError to find out more
	LARGE_INTEGER size;
	size.HighPart = fad.nFileSizeHigh;
	size.LowPart = fad.nFileSizeLow;
	return size.QuadPart;
}

void AccountSettings::Init()
{
	bool isPortable = false;
	CString str;
	LPTSTR ptr;
	accountId = 0;
	startTime = CTime::GetCurrentTime();
	//--
	ptr = exeFile.GetBuffer(MAX_PATH);
	GetModuleFileName(NULL, ptr, MAX_PATH);
	exeFile.ReleaseBuffer();
	//--
	pathExe = exeFile.Mid(0, exeFile.ReverseFind('\\'));
	//--
	CString fileName = PathFindFileName(exeFile);
	fileName = fileName.Mid(0, fileName.ReverseFind('.'));
	logFile.Format(_T("%s_log.txt"), fileName);
	iniFile.Format(_T("%s.ini"), fileName);
	pathRoaming = _T("");
	pathLocal = _T("");
	if (pathRoaming.IsEmpty()) {
		CString contactsFile = _T("Contacts.xml");
		CRegKey regKey;
		CString pathInstaller;
		CString rab;
		ULONG pnChars;
		rab.Format(_T("Software\\%s"), _T(_GLOBAL_NAME));
		if (regKey.Open(HKEY_CURRENT_USER, rab, KEY_READ) == ERROR_SUCCESS) {
			ptr = pathInstaller.GetBuffer(255);
			pnChars = 256;
			regKey.QueryStringValue(NULL, ptr, &pnChars);
			pathInstaller.ReleaseBuffer();
			regKey.Close();
		}
		if (pathInstaller.IsEmpty() && regKey.Open(HKEY_LOCAL_MACHINE, rab, KEY_READ) == ERROR_SUCCESS) {
			ptr = pathInstaller.GetBuffer(255);
			pnChars = 256;
			regKey.QueryStringValue(NULL, ptr, &pnChars);
			pathInstaller.ReleaseBuffer();
			regKey.Close();
		}
		if (pathInstaller.IsEmpty()) {
			ptr = pathInstaller.GetBuffer(255);
			DWORD bChars = 256;
			UINT e = MsiGetProductInfo(_T("{F6560769-8A2F-4874-9E76-D86B016CBDFF}"), INSTALLPROPERTY_PRODUCTNAME, ptr, &bChars);
			if (e == ERROR_SUCCESS) {
				pathInstaller = pathExe;
			}
			else {
				pathInstaller.ReleaseBuffer();
				pathInstaller.Empty();
			}
		}
		CString appDataRoaming;
		ptr = appDataRoaming.GetBuffer(MAX_PATH);
		SHGetSpecialFolderPath(
			0,
			ptr,
			CSIDL_APPDATA,
			FALSE);
		appDataRoaming.ReleaseBuffer();
		appDataRoaming.AppendFormat(_T("\\%s\\"), _T(_GLOBAL_NAME));

		if (!pathInstaller.IsEmpty() && pathInstaller.CompareNoCase(pathExe) == 0) {
			// installer
			CreateDirectory(appDataRoaming, NULL);
			pathRoaming = appDataRoaming;
			CString appDataLocal;
			ptr = appDataLocal.GetBuffer(MAX_PATH);
			SHGetSpecialFolderPath(
				0,
				ptr,
				CSIDL_LOCAL_APPDATA,
				FALSE);
			appDataLocal.ReleaseBuffer();
			appDataLocal.AppendFormat(_T("\\%s\\"), _T(_GLOBAL_NAME));
			CreateDirectory(appDataLocal, NULL);
			pathLocal = appDataLocal;
			logFile = pathLocal + logFile;
		}
		else {
			// portable
			isPortable = true;
			pathRoaming = pathExe + _T("\\");
			pathLocal = pathRoaming;
			// for version <= 3.14.5 move ini file from currdir to exedir
			CString pathCurrent;
			ptr = pathCurrent.GetBuffer(MAX_PATH);
			::GetCurrentDirectory(MAX_PATH, ptr);
			pathCurrent.ReleaseBuffer();
			if (pathCurrent.CompareNoCase(pathExe) != 0) {
				pathCurrent.Append(_T("\\"));
				if (CopyFile(pathCurrent + iniFile, pathRoaming + iniFile, TRUE)) {
					DeleteFile(pathCurrent + iniFile);
				}
				if (CopyFile(pathCurrent + contactsFile, pathRoaming + contactsFile, TRUE)) {
					DeleteFile(pathCurrent + contactsFile);
				}
				DeleteFile(pathCurrent + logFile);
			}
			// copy ini from installer path
			CopyFile(appDataRoaming + iniFile, pathRoaming + iniFile, TRUE);
			CopyFile(appDataRoaming + contactsFile, pathRoaming + contactsFile, TRUE);
			logFile = pathLocal + logFile;
		}

		iniFile = pathRoaming + iniFile;

		if (!::PathFileExists(iniFile) || FileSize(iniFile) == 0) {
			firstRun = true;
			// create UTF16-LE BOM(FFFE)
			WORD wBOM = 0xFEFF;
			CString pszSectionB = _T("[Settings]");
			CFile file;
			CFileException fileException;
			if (file.Open(iniFile, CFile::modeCreate | CFile::modeReadWrite, &fileException)) {
				file.Write(&wBOM, sizeof(wBOM));
				file.Write(pszSectionB.GetBuffer(), pszSectionB.GetLength() * sizeof(wchar_t));
				file.Close();
			}
		}
		else {
			firstRun = false;
			CFile file;
			CFileException fileException;
			if (file.Open(iniFile, CFile::modeReadWrite, &fileException)) {
				WORD wBOM;
				if (sizeof(WORD) == file.Read(&wBOM, sizeof(WORD))) {
					if (wBOM != 0xFEFF) {
						// convert to UTF16-LE BOM
						file.SeekToBegin();
						CStringA data;
						char buf[256];
						int count;
						while (true) {
							count = file.Read(buf, sizeof(buf));
							data.Append(buf, count);
							if (count < sizeof(buf)) {
								break;
							}
						}
						file.SetLength(0);
						wBOM = 0xFEFF;
						file.Write(&wBOM, sizeof(wBOM));
						CString res = AnsiToWideChar(data.GetBuffer());
						file.Write(res.GetBuffer(), data.GetLength() * sizeof(wchar_t));
					}
				}
				file.Close();
			}
		}
	}
	//--

	CString section;
	section = _T("Settings");

	ptr = defaultserver.GetBuffer(255);
	GetPrivateProfileString(section, _T("defaultserver"), _T(_GLOBAL_ACCOUNT_SIP_SERVER), ptr, 256, iniFile);
	defaultserver.ReleaseBuffer();
	ptr = defaultproxy.GetBuffer(255);
	GetPrivateProfileString(section, _T("defaultproxy"), _T(_GLOBAL_ACCOUNT_SIP_PROXY), ptr, 256, iniFile);
	defaultproxy.ReleaseBuffer();

	ptr = defaultdomain.GetBuffer(255);
	GetPrivateProfileString(section, _T("defaultdomain"), _T(_GLOBAL_ACCOUNT_DOMAIN), ptr, 256, iniFile);
	defaultdomain.ReleaseBuffer();

	ptr = updatesInterval.GetBuffer(255);
	GetPrivateProfileString(section, _T("updatesInterval"), NULL, ptr, 256, iniFile);
	updatesInterval.ReleaseBuffer();
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("checkUpdatesTime"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	checkUpdatesTime = _wtoi(str);

	ptr = portKnockerHost.GetBuffer(255);
	GetPrivateProfileString(section, _T("portKnockerHost"), NULL, ptr, 256, iniFile);
	portKnockerHost.ReleaseBuffer();

	ptr = portKnockerPorts.GetBuffer(255);
	GetPrivateProfileString(section, _T("portKnockerPorts"), NULL, ptr, 256, iniFile);
	portKnockerPorts.ReleaseBuffer();

	ptr = lastCallNumber.GetBuffer(255);
	GetPrivateProfileString(section, _T("lastCallNumber"), NULL, ptr, 256, iniFile);
	lastCallNumber.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("lastCallHasVideo"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	lastCallHasVideo = (str == _T("1"));

	enableLocalAccount = 0;

	crashReport = 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("DTMFMethod"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	DTMFMethod = _wtoi(str);
	//--
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("AA"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	AA = _wtoi(str);
	//--
	ptr = autoAnswer.GetBuffer(255);
	GetPrivateProfileString(section, _T("autoAnswer"), _T(_GLOBAL_AUTO_ANSWER_DEFAULT), ptr, 256, iniFile);
	autoAnswer.ReleaseBuffer();
	//--
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("DND"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	DND = _wtoi(str);
	//--
	ptr = denyIncoming.GetBuffer(255);
	GetPrivateProfileString(section, _T("denyIncoming"), _T(_GLOBAL_DENY_INCOMING_DEFAULT), ptr, 256, iniFile);
	denyIncoming.ReleaseBuffer();
	//--
	ptr = userAgent.GetBuffer(255);
	GetPrivateProfileString(section, _T("userAgent"), NULL, ptr, 256, iniFile);
	userAgent.ReleaseBuffer();

	ptr = usersDirectory.GetBuffer(255);
	GetPrivateProfileString(section, _T("usersDirectory"), _T(_GLOBAL_USERS_DIRECTORY_VALUE), ptr, 256, iniFile);
	usersDirectory.ReleaseBuffer();

	ptr = dnsSrvNs.GetBuffer(255);
	GetPrivateProfileString(section, _T("dnsSrvNs"), NULL, ptr, 256, iniFile);
	dnsSrvNs.ReleaseBuffer();
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("dnsSrv"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	dnsSrv = str == "1" ? 1 : 0;

	ptr = stun.GetBuffer(255);
	GetPrivateProfileString(section, _T("STUN"), NULL, ptr, 256, iniFile);
	stun.ReleaseBuffer();
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableSTUN"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	enableSTUN = str == "1" ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableMediaButtons"), _T("0"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	enableMediaButtons = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("localDTMF"), _T("1"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	localDTMF = _wtoi(str);

	ptr = ringtone.GetBuffer(255);
	GetPrivateProfileString(section, _T("ringingSound"), NULL, ptr, 256, iniFile);

	ringtone.ReleaseBuffer();
	ptr = audioRingDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioRingDevice"), NULL, ptr, 256, iniFile);
	audioRingDevice.ReleaseBuffer();
	ptr = audioOutputDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioOutputDevice"), NULL, ptr, 256, iniFile);
	audioOutputDevice.ReleaseBuffer();
	ptr = audioInputDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioInputDevice"), NULL, ptr, 256, iniFile);
	audioInputDevice.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("micAmplification"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	micAmplification = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("swLevelAdjustment"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	swLevelAdjustment = _wtoi(str);

	ptr = audioCodecs.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioCodecs"), _T(_GLOBAL_CODECS_HARDCODED), ptr, 256, iniFile);
	audioCodecs.ReleaseBuffer();
	if (isPortable) {
		str = _T("Recordings");
	}
	else {
		ptr = str.GetBuffer(MAX_PATH);
		SHGetSpecialFolderPath(
			0,
			ptr,
			CSIDL_DESKTOPDIRECTORY,
			FALSE);
		str.ReleaseBuffer();
		str.Append(_T("\\Recordings"));
	}
	ptr = recordingPath.GetBuffer(255);
	GetPrivateProfileString(section, _T("recordingPath"), str, ptr, 256, iniFile);
	recordingPath.ReleaseBuffer();

	ptr = str.GetBuffer(255);
		GetPrivateProfileString(section, _T("autoRecording"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	autoRecording = str == "1" ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("rport"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	rport = str == "0" ? 0 : 1;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("sourcePort"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	sourcePort = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("rtpPortMin"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	rtpPortMin = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("rtpPortMax"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	rtpPortMax = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("VAD"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	vad = str == "1" ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("EC"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	ec = str == _T("1") ? 1 : 0;

	//--
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("forceCodec"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	forceCodec = _wtoi(str);
	//--

#ifdef _GLOBAL_VIDEO
	ptr = videoCaptureDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoCaptureDevice"), NULL, ptr, 256, iniFile);
	videoCaptureDevice.ReleaseBuffer();

	ptr = videoCodec.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoCodec"), NULL, ptr, 256, iniFile);
	videoCodec.ReleaseBuffer();
	
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoH264"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoH264 = str == "0" ? 0 : 1;
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoH263"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoH263 = str == "0" ? 0 : 1;
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoVP8"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoVP8 = str == "0" ? 0 : 1;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoBitrate"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoBitrate = _wtoi(str);
#endif

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainX"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainX = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainY"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainY = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainW"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainW = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainH"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainH = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("noResize"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	noResize = str == _T("1") ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesX"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesX = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesY"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesY = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesW"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesW = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesH"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesH = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("ringinX"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	ringinX = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("ringinY"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	ringinY = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("volumeOutput"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	volumeOutput = str.IsEmpty() ? 100 : _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("volumeInput"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	volumeInput = str.IsEmpty() ? 100 : _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("activeTab"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	activeTab = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("alwaysOnTop"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	alwaysOnTop = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("autoHangUpTime"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	autoHangUpTime = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("maxConcurrentCalls"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	maxConcurrentCalls = _wtoi(str);

	ptr = cmdOutgoingCall.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdOutgoingCall"), NULL, ptr, 256, iniFile);
	cmdOutgoingCall.ReleaseBuffer();

	ptr = cmdIncomingCall.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdIncomingCall"), NULL, ptr, 256, iniFile);
	cmdIncomingCall.ReleaseBuffer();

	ptr = cmdCallRing.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallRing"), NULL, ptr, 256, iniFile);
	cmdCallRing.ReleaseBuffer();

	ptr = cmdCallAnswer.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallAnswer"), NULL, ptr, 256, iniFile);
	cmdCallAnswer.ReleaseBuffer();

	ptr = cmdCallBusy.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallBusy"), NULL, ptr, 256, iniFile);
	cmdCallBusy.ReleaseBuffer();

	ptr = cmdCallStart.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallStart"), NULL, ptr, 256, iniFile);
	cmdCallStart.ReleaseBuffer();

	ptr = cmdCallEnd.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallEnd"), NULL, ptr, 256, iniFile);
	cmdCallEnd.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableLog"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	enableLog = _wtoi(str);

	bringToFrontOnIncoming = atoi(_GLOBAL_BRING_TO_FRONT_VALUE);
	//--
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("randomAnswerBox"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	randomAnswerBox = _wtoi(str);

	//--
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("singleMode"), _T(_GLOBAL_SINGLE_MODE_VALUE), ptr, 256, iniFile);
	str.ReleaseBuffer();
	singleMode = _wtoi(str);

	hidden = 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("silent"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	silent = str == "1" ? 1 : 0;

	//--
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("accountId"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	if (str.IsEmpty()) {
		if (AccountLoad(-1, &account)) {
			accountId = 1;
			WritePrivateProfileString(section, _T("accountId"), _T("1"), iniFile);
		}
	}
	else {
		accountId = _wtoi(str);
		if (!accountId && !enableLocalAccount) {
			accountId = 1;
		}
		if (accountId > 0) {
			if (!AccountLoad(accountId, &account)) {
				accountId = 0;
			}
		}
	}
}

AccountSettings::AccountSettings()
{
	Init();
}

void AccountSettings::AccountDelete(int id)
{
	CString section;
	section.Format(_T("Account%d"), id);
	WritePrivateProfileStruct(section, NULL, NULL, 0, iniFile);
}

bool AccountSettings::AccountLoad(int id, Account *account)
{
	CString str;
	CString rab;
	LPTSTR ptr;

	CString section;
	if (id == -1) {
		section = _T("Settings");
}
	else {
		section.Format(_T("Account%d"), id);
	}

	ptr = account->label.GetBuffer(255);
	GetPrivateProfileString(section, _T("label"), _T(_GLOBAL_ACCOUNT_LABEL), ptr, 256, iniFile);
	account->label.ReleaseBuffer();

	ptr = account->server.GetBuffer(255);
	GetPrivateProfileString(section, _T("server"), NULL, ptr, 256, iniFile);
	account->server.ReleaseBuffer();
	if (account->server.IsEmpty()) {
		ptr = account->server.GetBuffer(255);
		GetPrivateProfileString(_T("Settings"), _T("defaultserver"), _T(_GLOBAL_ACCOUNT_SIP_SERVER), ptr, 256, iniFile);
		account->server.ReleaseBuffer();
	}

	ptr = account->proxy.GetBuffer(255);
	GetPrivateProfileString(section, _T("proxy"), NULL, ptr, 256, iniFile);
	account->proxy.ReleaseBuffer();
	if (account->proxy.IsEmpty()) {
		ptr = account->proxy.GetBuffer(255);
		GetPrivateProfileString(_T("Settings"), _T("defaultproxy"), _T(_GLOBAL_ACCOUNT_SIP_PROXY), ptr, 256, iniFile);
		account->proxy.ReleaseBuffer();
	}

	ptr = account->domain.GetBuffer(255);
	GetPrivateProfileString(section, _T("domain"), NULL, ptr, 256, iniFile);
	account->domain.ReleaseBuffer();
	if (account->domain.IsEmpty()) {
		ptr = account->domain.GetBuffer(255);
		GetPrivateProfileString(_T("Settings"), _T("defaultdomain"), _T(_GLOBAL_ACCOUNT_DOMAIN), ptr, 256, iniFile);
		account->domain.ReleaseBuffer();
	}

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("port"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->port = _wtoi(str);

	ptr = account->username.GetBuffer(255);
	GetPrivateProfileString(section, _T("username"), NULL, ptr, 256, iniFile);
	account->username.ReleaseBuffer();
	ptr = account->password.GetBuffer(1040);
	GetPrivateProfileString(section, _T("password"), NULL, ptr, 1041, iniFile);
	account->password.ReleaseBuffer();
	if (!account->password.IsEmpty()) {
		CByteArray arPassword;
		String2Bin(account->password, &arPassword);
		CCrypto crypto;
		if (crypto.DeriveKey((LPCTSTR)_GLOBAL_KEY)) {
			try {
				if (!crypto.Decrypt(arPassword, account->password)) {
					//--decode from old format
					ptr = str.GetBuffer(255);
					GetPrivateProfileString(section, _T("passwordSize"), NULL, ptr, 256, iniFile);
					str.ReleaseBuffer();
					if (!str.IsEmpty()) {
						int size = _wtoi(str);
						arPassword.SetSize(size > 0 ? size : 16);
						GetPrivateProfileStruct(section, _T("password"), arPassword.GetData(), arPassword.GetSize(), iniFile);
						crypto.Decrypt(arPassword, account->password);
					}
					//--end decode from old format
					if (crypto.Encrypt(account->password, arPassword)) {
						WritePrivateProfileString(section, _T("password"), Bin2String(&arPassword), iniFile);
						//--delete old format addl.data
						WritePrivateProfileString(section, _T("passwordSize"), NULL, iniFile);
					}
				}
			}
			catch (CArchiveException *e) {
			}
		}
					}

	account->rememberPassword = !account->username.GetLength() || account->password.GetLength() ? 1 : 0;


	ptr = account->authID.GetBuffer(255);
	GetPrivateProfileString(section, _T("authID"), _T(_GLOBAL_ACCOUNT_LOGIN), ptr, 256, iniFile);
	account->authID.ReleaseBuffer();

	ptr = account->displayName.GetBuffer(255);
	GetPrivateProfileString(section, _T("displayName"), _T(_GLOBAL_ACCOUNT_NAME), ptr, 256, iniFile);
	account->displayName.ReleaseBuffer();

	ptr = account->voicemailNumber.GetBuffer(255);
	GetPrivateProfileString(section, _T("voicemailNumber"), _T(_GLOBAL_ACCOUNT_VM_NUMBER), ptr, 256, iniFile);
	account->voicemailNumber.ReleaseBuffer();

	ptr = account->srtp.GetBuffer(255);
	GetPrivateProfileString(section, _T("SRTP"), _T(_GLOBAL_ACCOUNT_SRTP), ptr, 256, iniFile);
	account->srtp.ReleaseBuffer();

	ptr = account->transport.GetBuffer(255);
	GetPrivateProfileString(section, _T("transport"), _T(_GLOBAL_ACCOUNT_TRANSPORT), ptr, 256, iniFile);
	account->transport.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	rab.Format(_T("%d"), _GLOBAL_ACCOUNT_PUBLISH);
	GetPrivateProfileString(section, _T("publish"), rab, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->publish = str == _T("1") ? 1 : 0;

	ptr = str.GetBuffer(255);
	rab.Format(_T("%d"), _GLOBAL_ACCOUNT_ICE);
	GetPrivateProfileString(section, _T("ICE"), rab, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->ice = str == _T("1") ? 1 : 0;

	ptr = str.GetBuffer(255);
	rab.Format(_T("%d"), _GLOBAL_ACCOUNT_ALLOW_REWRITE);
	GetPrivateProfileString(section, _T("allowRewrite"), rab, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->allowRewrite = str == _T("1") ? 1 : 0;


	ptr = str.GetBuffer(255);
	rab.Format(_T("%d"), _GLOBAL_ACCOUNT_DISABLE_SESSION_TIMER);
	GetPrivateProfileString(section, _T("disableSessionTimer"), rab, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->disableSessionTimer = str == _T("1") ? 1 : 0;

	bool sectionExists = IniSectionExists(section, iniFile);

	if (id == -1) {
		// delete old
		WritePrivateProfileString(section, _T("server"), NULL, iniFile);
		WritePrivateProfileString(section, _T("proxy"), NULL, iniFile);
		WritePrivateProfileString(section, _T("SRTP"), NULL, iniFile);
		WritePrivateProfileString(section, _T("transport"), NULL, iniFile);
		WritePrivateProfileString(section, _T("publicAddr"), NULL, iniFile);
		WritePrivateProfileString(section, _T("publish"), NULL, iniFile);
		WritePrivateProfileString(section, _T("STUN"), NULL, iniFile);
		WritePrivateProfileString(section, _T("ICE"), NULL, iniFile);
		WritePrivateProfileString(section, _T("allowRewrite"), NULL, iniFile);
		WritePrivateProfileString(section, _T("domain"), NULL, iniFile);
		WritePrivateProfileString(section, _T("authID"), NULL, iniFile);
		WritePrivateProfileString(section, _T("username"), NULL, iniFile);
		WritePrivateProfileString(section, _T("passwordSize"), NULL, iniFile);
		WritePrivateProfileString(section, _T("password"), NULL, iniFile);
		WritePrivateProfileString(section, _T("id"), NULL, iniFile);
		WritePrivateProfileString(section, _T("displayName"), NULL, iniFile);
		// save new
		//if (!account->domain.IsEmpty() && !account->username.IsEmpty()) {
		if (sectionExists && !account->domain.IsEmpty()) {
			AccountSave(1, account);
	}
				}
	//return !account->domain.IsEmpty() && !account->username.IsEmpty();
	return  sectionExists && !account->domain.IsEmpty();
			}

void AccountSettings::AccountSave(int id, Account *account)
{
	CString str;
	CString section;
	section.Format(_T("Account%d"), id);

	WritePrivateProfileString(section, _T("label"), account->label, iniFile);

	WritePrivateProfileString(section, _T("server"), account->server, iniFile);

	WritePrivateProfileString(section, _T("proxy"), account->proxy, iniFile);

	WritePrivateProfileString(section, _T("domain"), account->domain, iniFile);

	str.Format(_T("%d"), account->port);
	WritePrivateProfileString(section, _T("port"), str, iniFile);

	if (!account->rememberPassword) {
		WritePrivateProfileString(section, _T("username"), _T(""), iniFile);
		WritePrivateProfileString(section, _T("password"), _T(""), iniFile);
	}
	else {
		WritePrivateProfileString(section, _T("username"), account->username, iniFile);
		CCrypto crypto;
		CByteArray arPassword;
		if (!account->password.IsEmpty() && crypto.DeriveKey((LPCTSTR)_GLOBAL_KEY)
			&& crypto.Encrypt(account->password, arPassword)
			) {
			str = Bin2String(&arPassword);
		}
		else {
			str = account->password;
	}
		WritePrivateProfileString(section, _T("password"), str, iniFile);
	}

	WritePrivateProfileString(section, _T("authID"), account->authID, iniFile);

	WritePrivateProfileString(section, _T("displayName"), account->displayName, iniFile);

	WritePrivateProfileString(section, _T("voicemailNumber"), account->voicemailNumber, iniFile);

	WritePrivateProfileString(section, _T("transport"), account->transport, iniFile);
	WritePrivateProfileString(section, _T("SRTP"), account->srtp, iniFile);
	WritePrivateProfileString(section, _T("publish"), account->publish ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("ICE"), account->ice ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("allowRewrite"), account->allowRewrite ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("disableSessionTimer"), account->disableSessionTimer ? _T("1") : _T("0"), iniFile);
	}

void AccountSettings::SettingsSave()
{
	CString str;
	LPTSTR ptr;

	CString section;
	section = _T("Settings");

	WritePrivateProfileString(section, _T("defaultserver"), defaultserver, iniFile);
	WritePrivateProfileString(section, _T("defaultproxy"), defaultproxy, iniFile);
	WritePrivateProfileString(section, _T("defaultdomain"), defaultdomain, iniFile);

	str.Format(_T("%d"), accountId);
	WritePrivateProfileString(section, _T("accountId"), str, iniFile);

	WritePrivateProfileString(section, _T("enableLog"), enableLog ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("randomAnswerBox"), randomAnswerBox ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("portKnockerHost"), portKnockerHost, iniFile);

	WritePrivateProfileString(section, _T("portKnockerPorts"), portKnockerPorts, iniFile);

	WritePrivateProfileString(section, _T("lastCallNumber"), lastCallNumber, iniFile);
	WritePrivateProfileString(section, _T("lastCallHasVideo"), lastCallHasVideo ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("updatesInterval"), updatesInterval, iniFile);
	str.Format(_T("%d"), checkUpdatesTime);
	WritePrivateProfileString(section, _T("checkUpdatesTime"), str, iniFile);

	WritePrivateProfileString(section, _T("DTMFMethod"), DTMFMethod == 1 ? _T("1") : (DTMFMethod == 2 ? _T("2") : (DTMFMethod == 3 ? _T("3") : _T("0"))), iniFile);

	WritePrivateProfileString(section, _T("AA"), AA ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("autoAnswer"), autoAnswer, iniFile);

	WritePrivateProfileString(section, _T("DND"), DND ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("denyIncoming"), denyIncoming, iniFile);

	WritePrivateProfileString(section, _T("usersDirectory"), usersDirectory, iniFile);

	WritePrivateProfileString(section, _T("dnsSrvNs"), dnsSrvNs, iniFile);
	WritePrivateProfileString(section, _T("dnsSrv"), dnsSrv ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("STUN"), stun, iniFile);

	WritePrivateProfileString(section, _T("enableSTUN"), enableSTUN ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("singleMode"), singleMode ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("rport"), rport ? _T("1") : _T("0"), iniFile);
	str.Format(_T("%d"), sourcePort);
	WritePrivateProfileString(section, _T("sourcePort"), str, iniFile);
	str.Format(_T("%d"), rtpPortMin);
	WritePrivateProfileString(section, _T("rtpPortMin"), str, iniFile);
	str.Format(_T("%d"), rtpPortMax);
	WritePrivateProfileString(section, _T("rtpPortMax"), str, iniFile);

	WritePrivateProfileString(section, _T("silent"), silent ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("enableMediaButtons"), enableMediaButtons ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("localDTMF"), localDTMF ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("ringingSound"), ringtone, iniFile);
	WritePrivateProfileString(section, _T("audioRingDevice"), _T("\"") + audioRingDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("audioOutputDevice"), _T("\"") + audioOutputDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("audioInputDevice"), _T("\"") + audioInputDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("micAmplification"), micAmplification ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("swLevelAdjustment"), swLevelAdjustment ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("audioCodecs"), audioCodecs, iniFile);
	WritePrivateProfileString(section, _T("VAD"), vad ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("EC"), ec ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("forceCodec"), forceCodec ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("recordingPath"), recordingPath, iniFile);
	WritePrivateProfileString(section, _T("autoRecording"), autoRecording ? _T("1") : _T("0"), iniFile);
#ifdef _GLOBAL_VIDEO
	WritePrivateProfileString(section, _T("videoCaptureDevice"), _T("\"") + videoCaptureDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("videoCodec"), videoCodec, iniFile);
	WritePrivateProfileString(section, _T("videoH264"), videoH264 ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("videoH263"), videoH263 ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("videoVP8"), videoVP8 ? _T("1") : _T("0"), iniFile);
	str.Format(_T("%d"), videoBitrate);
	WritePrivateProfileString(section, _T("videoBitrate"), str, iniFile);
#endif

	str.Format(_T("%d"), mainX);
	WritePrivateProfileString(section, _T("mainX"), str, iniFile);

	str.Format(_T("%d"), mainY);
	WritePrivateProfileString(section, _T("mainY"), str, iniFile);

	str.Format(_T("%d"), mainW);
	WritePrivateProfileString(section, _T("mainW"), str, iniFile);

	str.Format(_T("%d"), mainH);
	WritePrivateProfileString(section, _T("mainH"), str, iniFile);

	str.Format(_T("%d"), noResize);
	WritePrivateProfileString(section, _T("noResize"), str, iniFile);

	str.Format(_T("%d"), messagesX);
	WritePrivateProfileString(section, _T("messagesX"), str, iniFile);

	str.Format(_T("%d"), messagesY);
	WritePrivateProfileString(section, _T("messagesY"), str, iniFile);

	str.Format(_T("%d"), messagesW);
	WritePrivateProfileString(section, _T("messagesW"), str, iniFile);

	str.Format(_T("%d"), messagesH);
	WritePrivateProfileString(section, _T("messagesH"), str, iniFile);

	str.Format(_T("%d"), ringinX);
	WritePrivateProfileString(section, _T("ringinX"), str, iniFile);

	str.Format(_T("%d"), ringinY);
	WritePrivateProfileString(section, _T("ringinY"), str, iniFile);

	str.Format(_T("%d"), volumeOutput);
	WritePrivateProfileString(section, _T("volumeOutput"), str, iniFile);

	str.Format(_T("%d"), volumeInput);
	WritePrivateProfileString(section, _T("volumeInput"), str, iniFile);

	str.Format(_T("%d"), activeTab);
	WritePrivateProfileString(section, _T("activeTab"), str, iniFile);

	str.Format(_T("%d"), alwaysOnTop);
	WritePrivateProfileString(section, _T("alwaysOnTop"), str, iniFile);

	str.Format(_T("%d"), autoHangUpTime);
	WritePrivateProfileString(section, _T("autoHangUpTime"), str, iniFile);

	str.Format(_T("%d"), maxConcurrentCalls);
	WritePrivateProfileString(section, _T("maxConcurrentCalls"), str, iniFile);

	WritePrivateProfileString(section, _T("cmdOutgoingCall"), cmdOutgoingCall, iniFile);
	WritePrivateProfileString(section, _T("cmdIncomingCall"), cmdIncomingCall, iniFile);
	WritePrivateProfileString(section, _T("cmdCallRing"), cmdCallRing, iniFile);
	WritePrivateProfileString(section, _T("cmdCallAnswer"), cmdCallAnswer, iniFile);
	WritePrivateProfileString(section, _T("cmdCallBusy"), cmdCallBusy, iniFile);
	WritePrivateProfileString(section, _T("cmdCallStart"), cmdCallStart, iniFile);
	WritePrivateProfileString(section, _T("cmdCallEnd"), cmdCallEnd, iniFile);
}
