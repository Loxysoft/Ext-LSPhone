#include "stdafx.h"

#include "CListCtrl_SortItemsEx.h"
#include "Calls.h"
#include "mainDlg.h"

BEGIN_MESSAGE_MAP(CListCtrl_SortItemsEx, CListCtrl)
	ON_NOTIFY_REFLECT_EX(LVN_COLUMNCLICK, OnHeaderClick)	// Column Click
END_MESSAGE_MAP()

namespace {
	struct PARAMSORT
	{
		PARAMSORT(HWND hWnd, int columnIndex, bool ascending)
			:m_hWnd(hWnd)
			,m_ColumnIndex(columnIndex)
			,m_Ascending(ascending)
		{}

		HWND m_hWnd;
		int  m_ColumnIndex;
		bool m_Ascending;
	};

	// Comparison extracts values from the List-Control
	int CALLBACK SortFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
	{
		PARAMSORT& ps = *(PARAMSORT*)lParamSort;
		TCHAR left[256] = _T(""), right[256] = _T("");
		ListView_GetItemText(ps.m_hWnd, lParam1, ps.m_ColumnIndex, left, sizeof(left));
		ListView_GetItemText(ps.m_hWnd, lParam2, ps.m_ColumnIndex, right, sizeof(right));	
		if (ps.m_Ascending)
			return _tcscmp( left, right );
		else
			return _tcscmp( right, left );
	}
}

bool CListCtrl_SortItemsEx::SortColumn(int columnIndex, bool ascending)
{
	PARAMSORT paramsort(m_hWnd, columnIndex, ascending);
	ListView_SortItemsEx(m_hWnd, SortFunc, &paramsort);
	return true;
}
