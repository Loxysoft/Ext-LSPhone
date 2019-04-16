/*
* Copyright (C) 2011-2019 MicroSIP (http://www.microsip.org)
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

#include "Hid.h"

#include <SetupAPI.h>
#pragma comment(lib, "Setupapi.lib")

#include <string.h>

#undef THIS_FILENAME_ADDON
#define THIS_FILENAME_ADDON   "Hid.cpp"

hid_device *Hid::m_hDevice = NULL;
unsigned int Hid::m_PrevHookState = HOOK_STATE_UNK;

void CALLBACK Hid::TimerProc(HWND hWnd, UINT nMsg, UINT_PTR nIDEvent, DWORD dwTime)
{
	if (!m_hDevice) {
		return;
	}

	int ReportLength = m_hDevice->input_report_length;
	unsigned char* Report = (unsigned char*)m_hDevice->read_buf;
	ReportLength = hid_read(m_hDevice, Report, ReportLength);

	if (ReportLength > 0) {
		UCHAR ReportId = Report[0];

		PJ_LOG(3, (THIS_FILENAME_ADDON, "IN --- Received report %d, length %d bytes ---", ReportId, ReportLength));

		USAGE UsagePage = 0x0b;
		USAGE UsageList[32];
		ULONG UsageLength = 32;
		NTSTATUS status = HidP_GetUsages(
			HidP_Input,
			UsagePage,
			0,
			UsageList,
			&UsageLength,
			m_hDevice->pp_data,
			(PCHAR)Report,
			ReportLength
		);
		if (status != HIDP_STATUS_SUCCESS) {
			PJ_LOG(3, (THIS_FILENAME_ADDON, "HidP_GetUsages failed with code 0x%04hx", status));
		} else {
			PJ_LOG(3, (THIS_FILENAME_ADDON, "IN get %d Usage(s):", UsageLength));
			bool offHook = false;
			bool lineBusy = false;
			for (unsigned int j = 0; j < UsageLength; j++) {
						PJ_LOG(3, (THIS_FILENAME_ADDON, "IN %02hhx", UsageList[j]));
						switch (UsageList[j]) {
						case 0x20: // Hook Switch
							offHook = true;
							break;
						case 0x2F: // Phone Mute
							//--
							//mainDlg->pageDialer->OnBnClickedMuteInput();
							//--
							break;
						case 0x24: // Redial
							//--
							PJ_LOG(3, (THIS_FILENAME_ADDON, "IN event Redial"));
							mainDlg->pageDialer->OnBnClickedRedial();
							//--
							break;
						case 0x21: // Flash (call swap between an active call and a call on hold)
							PJ_LOG(3, (THIS_FILENAME_ADDON, "IN event Flash"));
							break;
						case 0x97: // Line Busy (1 = Line is busy / 0 = Line is free)
							lineBusy = true;
							break;
						}
			}
			//--
			HIDP_BUTTON_CAPS    ButtonCaps[32];
			USHORT              ButtonCapsLength = 32;
			NTSTATUS status = HidP_GetSpecificButtonCaps(
				HidP_Input,
				UsagePage,
				0,
				0x20,
				ButtonCaps,
				&ButtonCapsLength,
				m_hDevice->pp_data
			);
			if (status != HIDP_STATUS_SUCCESS) {
				PJ_LOG(3, (THIS_FILENAME_ADDON, "HidP_GetSpecificButtonCaps failed with code 0x%04hx", status));
			} else {
				if (ButtonCapsLength <= 0) {
					PJ_LOG(3, (THIS_FILENAME_ADDON, "Hook Switch not found"));
				}
				else {
					bool offHookIsAbsolute = false;
					for (int i = 0; i < ButtonCapsLength; i++) {
						if (ButtonCaps[i].ReportID == ReportId) {
							offHookIsAbsolute = ButtonCaps[i].IsAbsolute;
							break;
						}
					}
					PJ_LOG(3, (THIS_FILENAME_ADDON, "IN hook switch is %s", offHookIsAbsolute ? "absolute" : "relative"));
					if (!offHookIsAbsolute) {
						if (!offHook) {
							offHook = m_PrevHookState == HOOK_STATE_OFF;
						}
						else {
							offHook = m_PrevHookState == HOOK_STATE_ON;
						}
					}
					else {
						if (m_PrevHookState == HOOK_STATE_OFF && !offHook && !lineBusy) {
							offHook = true;
						}
					}

					if (offHook) {
						if (m_PrevHookState == HOOK_STATE_ON) {
							// activate call
							//!!m_PrevHookState = HOOK_STATE_OFF;
							PJ_LOG(3, (THIS_FILENAME_ADDON, "IN Event Off-Hook"));
							if (mainDlg->ringinDlgs.GetCount()) {
								// accept first incoming call
								RinginDlg *ringinDlg = mainDlg->ringinDlgs.GetAt(0);
								mainDlg->PostMessage(UM_CALL_ANSWER, (WPARAM)ringinDlg->call_id, (LPARAM)0);
							}
							else {
								// dial number
								mainDlg->pageDialer->Action(ACTION_CALL);
							}
							//--
						}
						else {
							PJ_LOG(3, (THIS_FILENAME_ADDON, "IN no event"));
						}
					}
					else {
						if (m_PrevHookState == HOOK_STATE_OFF) {
							// stop call
							//!!m_PrevHookState = HOOK_STATE_ON;
							PJ_LOG(3, (THIS_FILENAME_ADDON, "IN Event On-Hook"));
							//							if (call_get_count_noincoming()) {
							// end all active and not on-hold calls
							call_hangup_all_noincoming(true);
							//							}
						}
						else {
							PJ_LOG(3, (THIS_FILENAME_ADDON, "IN no event"));
						}
					}
				}
			}
			//--
		}
	}
	if (ReportLength < 0) {
		PJ_LOG(3, (THIS_FILENAME_ADDON, "hid_read() returned error"));
		return;
	}
}

void Hid::SetLED(bool state)
{
	if (!m_hDevice) {
		return;
	}

	if (
		(state && m_PrevHookState != HOOK_STATE_OFF)
		||
		(!state && m_PrevHookState != HOOK_STATE_ON)
		) {

		m_PrevHookState = state ? HOOK_STATE_OFF : HOOK_STATE_ON;
		PJ_LOG(3, (THIS_FILENAME_ADDON, "Set state: %s", state ? "Off-Hook" : "On-Hook"));

		PJ_LOG(3, (THIS_FILENAME_ADDON, "OUT --- Set LED state %d ---", state));

		ULONG ReportLength = m_hDevice->caps.OutputReportByteLength;
		PJ_LOG(3, (THIS_FILENAME_ADDON, "OUT Report length %d", ReportLength));
		if (ReportLength <= 0) {
			return;
		}
		unsigned char *Report = new (nothrow)unsigned char[ReportLength];
		if (Report == nullptr) {
			return;
		}
		USAGE UsagePage = 0x08;
		USAGE Usage = 0x17;
		HIDP_BUTTON_CAPS    ButtonCaps[32];
		USHORT              ButtonCapsLength = 32;
		NTSTATUS status = HidP_GetSpecificButtonCaps(
			HidP_Output,
			UsagePage,
			0,
			Usage,
			ButtonCaps,
			&ButtonCapsLength,
			m_hDevice->pp_data
		);
		if (status != HIDP_STATUS_SUCCESS) {
			PJ_LOG(3, (THIS_FILENAME_ADDON, "HidP_GetSpecificButtonCaps failed with code 0x%04hx", status));
		}
		else {
			PJ_LOG(3, (THIS_FILENAME_ADDON, "LEDs Off-Hook caps length: %d", ButtonCapsLength));
			for (int i = 0; i < ButtonCapsLength; i++) {
				ZeroMemory(Report, ReportLength);
				Report[0] = ButtonCaps[i].ReportID;
				USAGE UsageList[1];
				ULONG UsageLength = state ? 1 : 0;
				UsageList[0] = Usage;
				status = HidP_SetUsages(
					HidP_Output,
					UsagePage,
					0,
					UsageList,
					&UsageLength,
					m_hDevice->pp_data,
					(PCHAR)Report,
					ReportLength
				);
				if (status != HIDP_STATUS_SUCCESS) {
					PJ_LOG(3, (THIS_FILENAME_ADDON, "HidP_SetUsages failed with code 0x%04hx, trace:", status));
					for (int j = 0; j < ReportLength; j++) {
						PJ_LOG(3, (THIS_FILENAME_ADDON, "0x%02hx", Report[j]));
					}
					Report[1] = state ? 0x01 : 0;
					PJ_LOG(3, (THIS_FILENAME_ADDON, "Set directly"));
					status = HIDP_STATUS_SUCCESS;
				}
				if (status == HIDP_STATUS_SUCCESS) {
					PJ_LOG(3, (THIS_FILENAME_ADDON, "OUT set %d Usage(s)", UsageLength));
					ULONG BytesWritten = hid_write(m_hDevice, Report, ReportLength);
					if (BytesWritten < 0) {
						PJ_LOG(3, (THIS_FILENAME_ADDON, "hid_write() returned error %ls", hid_error(m_hDevice)));
						return;
					}
				}
			}
		}
		delete[] Report;
	}
}

void Hid::OpenDevice()
{
	if (m_hDevice) {
		PJ_LOG(3, (THIS_FILENAME_ADDON, "Device already opened"));
		return;
	}
	struct hid_device_info *devs, *cur_dev, *sel_dev = NULL;
	devs = hid_enumerate(0x0, 0x0);
	cur_dev = devs;
	while (cur_dev) {
		PJ_LOG(3, (THIS_FILENAME_ADDON, "Device Found %04hx:%04hx (usage: %04hx:%04hx)", cur_dev->vendor_id, cur_dev->product_id, cur_dev->usage_page, cur_dev->usage));
		PJ_LOG(3, (THIS_FILENAME_ADDON, "  Manufacturer: %ls", cur_dev->manufacturer_string));
		PJ_LOG(3, (THIS_FILENAME_ADDON, "  Product:      %ls", cur_dev->product_string));
		if (!sel_dev && cur_dev->usage_page == 0x0b) {
			sel_dev = cur_dev;
		}
		if (cur_dev->usage_page == 0x0b && (cur_dev->usage == 0x05 || cur_dev->usage == 0x04)) {
			sel_dev = cur_dev;
		}
		cur_dev = cur_dev->next;
	}
	if (!sel_dev) {
		PJ_LOG(3, (THIS_FILENAME_ADDON, "Telephony Device not found"));
	}
	else {
		PJ_LOG(3, (THIS_FILENAME_ADDON, "Device Selected %04hx:%04hx (usage: %04hx:%04hx)", sel_dev->vendor_id, sel_dev->product_id, sel_dev->usage_page, sel_dev->usage));
		hid_device *m_hDevice_tmp = NULL;
		m_hDevice = hid_open_path(sel_dev->path);
		if (!m_hDevice) {
			PJ_LOG(3, (THIS_FILENAME_ADDON, "hid_open_path() returned error"));
		}
		else {
			PJ_LOG(3, (THIS_FILENAME_ADDON, "Device Opened %02hhx %02hhx /iof-rl %d %d %d /nlc %d /ni-bvd %d %d %d /no-bvd %d %d %d /nf-bvd %d %d %d",
			m_hDevice->caps.UsagePage,
			m_hDevice->caps.Usage,
			m_hDevice->caps.InputReportByteLength,
			m_hDevice->caps.OutputReportByteLength,
			m_hDevice->caps.FeatureReportByteLength,
			m_hDevice->caps.NumberLinkCollectionNodes,
			m_hDevice->caps.NumberInputButtonCaps,
			m_hDevice->caps.NumberInputValueCaps,
			m_hDevice->caps.NumberInputDataIndices,
			m_hDevice->caps.NumberOutputButtonCaps,
			m_hDevice->caps.NumberOutputValueCaps,
			m_hDevice->caps.NumberOutputDataIndices,
			m_hDevice->caps.NumberFeatureButtonCaps,
			m_hDevice->caps.NumberFeatureValueCaps,
			m_hDevice->caps.NumberFeatureDataIndices
				));
			// Set the hid_read() function to be non-blocking.
			hid_set_nonblocking(m_hDevice, 1);

			HIDP_BUTTON_CAPS    ButtonCaps[256];
			USHORT              ButtonCapsLength;

			ButtonCapsLength = 256;
			HidP_GetButtonCaps(
				HidP_Input,
				ButtonCaps,
				&ButtonCapsLength,
				m_hDevice->pp_data
			);
			PJ_LOG(3, (THIS_FILENAME_ADDON, "Input ButtonCaps %d:", ButtonCapsLength));
			for (int i = 0; i < ButtonCapsLength; i++) {
				PJ_LOG(3, (THIS_FILENAME_ADDON, "%02hhx %d %d %d %d %d %d %02hhx %02hhx %02hhx",
					ButtonCaps[i].UsagePage,
					ButtonCaps[i].ReportID,
					ButtonCaps[i].IsAlias,
					ButtonCaps[i].IsRange,
					ButtonCaps[i].IsStringRange,
					ButtonCaps[i].IsDesignatorRange,
					ButtonCaps[i].IsAbsolute,
					ButtonCaps[i].Range.UsageMin,
					ButtonCaps[i].Range.UsageMax,
					ButtonCaps[i].NotRange.Usage
					));
			}

			ButtonCapsLength = 256;
			HidP_GetButtonCaps(
				HidP_Output,
				ButtonCaps,
				&ButtonCapsLength,
				m_hDevice->pp_data
			);
			PJ_LOG(3, (THIS_FILENAME_ADDON, "Output ButtonCaps %d:", ButtonCapsLength));
			for (int i = 0; i < ButtonCapsLength; i++) {
				PJ_LOG(3, (THIS_FILENAME_ADDON, "%02hhx %d %d %d %d %d %d %02hhx %02hhx %02hhx",
					ButtonCaps[i].UsagePage,
					ButtonCaps[i].ReportID,
					ButtonCaps[i].IsAlias,
					ButtonCaps[i].IsRange,
					ButtonCaps[i].IsStringRange,
					ButtonCaps[i].IsDesignatorRange,
					ButtonCaps[i].IsAbsolute,
					ButtonCaps[i].Range.UsageMin,
					ButtonCaps[i].Range.UsageMax,
					ButtonCaps[i].NotRange.Usage
					));
			}

			SetLED(false);

			mainDlg->SetTimer(IDT_TIMER_HEADSET, 200, &TimerProc);
		}
	}
	hid_free_enumeration(devs);
}

void Hid::CloseDevice()
{
	if (!m_hDevice) {
		return;
	}
	mainDlg->KillTimer(IDT_TIMER_HEADSET);

	PJ_LOG(3, (THIS_FILENAME_ADDON, "Device Closed"));

	hid_close(m_hDevice);
	/* Free static HIDAPI objects. */
	hid_exit();
	m_hDevice = NULL;
}
