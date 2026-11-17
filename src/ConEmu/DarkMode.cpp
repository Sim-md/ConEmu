#define HIDE_USE_EXCEPTION_INFO
#include "Header.h"
#include <commctrl.h>
#include <cmath>
#include "ConEmu.h"
#include "DarkMode.h"
#include "SetDlgColors.h"
#include "DlgItemHelper.h"
#include "Options.h"
#include "RealConsole.h"
#include "VirtualConsole.h"

namespace DarkMode
{
namespace
{
constexpr UINT_PTR SubclassId = 0xCEda;
constexpr COLORREF Border = RGB(100, 100, 100);
constexpr COLORREF Accent = RGB(110, 170, 220);

bool IsClass(HWND window, LPCWSTR expected)
{
	wchar_t name[64] = {};
	GetClassName(window, name, countof(name));
	return lstrcmpi(name, expected) == 0;
}

constexpr int FocusInset = 3;
constexpr int FocusTextGap = 2;

RECT FocusTextBounds(RECT content, const RECT& focus)
{
	content.left = std::max(content.left, focus.left + FocusTextGap);
	content.right = std::min(content.right, focus.right - FocusTextGap);
	return content;
}

void PaintFocusRect(HDC dc, const RECT& bounds)
{
	if (!IsRectEmpty(&bounds))
		DrawFocusRect(dc, &bounds);
}

void PaintButton(HWND window, HDC dc)
{
	const int saved = SaveDC(dc);
	RECT rc; GetClientRect(window, &rc);
	const DWORD style = GetWindowLong(window, GWL_STYLE);
	const DWORD type = style & BS_TYPEMASK;
	const LRESULT state = SendMessage(window, BM_GETSTATE, 0, 0);
	const bool enabled = IsWindowEnabled(window) != FALSE;
	const bool pressed = (state & BST_PUSHED) != 0;
	POINT cursor; GetCursorPos(&cursor); ScreenToClient(window, &cursor);
	const bool hot = enabled && PtInRect(&rc, cursor);
	const bool check = type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE;
	const bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
	const bool group = type == BS_GROUPBOX;
	const bool push = !group && (!(check || radio) || (style & BS_PUSHLIKE));
	SelectObject(dc, reinterpret_cast<HFONT>(SendMessage(window, WM_GETFONT, 0, 0)));
	SetTextColor(dc, enabled ? Text : Hint);
	SetBkMode(dc, TRANSPARENT);
	wchar_t text[1024] = {}; GetWindowText(window, text, countof(text));
	TEXTMETRIC metrics = {}; GetTextMetrics(dc, &metrics);
	const int unit = std::max(13L, metrics.tmHeight);
	if (check && !push && !(style & (BS_LEFTTEXT | BS_MULTILINE)))
	{
		// A checkbox used as a group caption must only cover the frame under its text.
		RECT textBounds{};
		DrawText(dc, text, -1, &textBounds, DT_CALCRECT | DT_SINGLELINE);
		rc.right = std::min(rc.right, rc.left + textBounds.right + unit + 10);
	}
	// Controls inside a group box are siblings and may have already painted.
	// Let the dialog paint the background and leave the group's interior alone.
	if (!group)
		FillRect(dc, &rc, Brush(dc, push ? (pressed ? RGB(80,80,80) : hot ? Hover : Control) : Background));
	// A group frame is decorative. Scrolling can repaint it under a stationary pointer.
	const bool accentBorder = !group && (hot || GetFocus() == window || type == BS_DEFPUSHBUTTON);
	HPEN pen = CreatePen(PS_SOLID, 1, accentBorder ? Accent : Border);
	HGDIOBJ oldPen = SelectObject(dc, pen);
	HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	RECT label = rc;
	if (group)
	{
		Rectangle(dc, 0, metrics.tmHeight/2, rc.right, rc.bottom);
		label.left += unit / 2; label.bottom = label.top + metrics.tmHeight;
		if (text[0])
		{
			RECT bounds = label;
			DrawText(dc, text, -1, &bounds, DT_CALCRECT | DT_SINGLELINE);
			bounds.right = std::min(bounds.right + 4, rc.right);
			FillRect(dc, &bounds, Brush(dc, Background));
		}
	}
	else if (push)
	{
		RoundRect(dc, 0, 0, rc.right, rc.bottom, 5, 5);
		InflateRect(&label, -4, -2);
	}
	else
	{
		const int x = (style & BS_LEFTTEXT) ? rc.right - unit : 1;
		const int y = (rc.bottom - unit) / 2;
		if (radio) Ellipse(dc, x, y, x + unit, y + unit);
		else Rectangle(dc, x, y, x + unit, y + unit);
		const LRESULT checked = SendMessage(window, BM_GETCHECK, 0, 0);
		if (checked)
		{
			RECT mark = {x + 3, y + 3, x + unit - 3, y + unit - 3};
			if (radio)
			{
				SelectObject(dc, Brush(dc, enabled ? Accent : Hint));
				Ellipse(dc, mark.left, mark.top, mark.right, mark.bottom);
				SelectObject(dc, GetStockObject(NULL_BRUSH));
			}
			else if (checked == BST_INDETERMINATE)
				FillRect(dc, &mark, Brush(dc, Hint));
			else
			{
				HPEN checkPen = CreatePen(PS_SOLID, 2, enabled ? Accent : Hint);
				HGDIOBJ previous = SelectObject(dc, checkPen);
				MoveToEx(dc, x+3, y+unit/2, nullptr);
				LineTo(dc, x+unit/2, y+unit-4); LineTo(dc, x+unit-3, y+3);
				SelectObject(dc, previous); DeleteObject(checkPen);
			}
		}
		if (style & BS_LEFTTEXT) label.right -= unit + 5;
		else label.left += unit + 5;
	}
	RECT focus = rc;
	if (!group)
	{
		InflateRect(&focus, -FocusInset, 0);
		if (!push)
		{
			// Keep the frame in the label area, clear of the checkbox or radio glyph.
			if (style & BS_LEFTTEXT)
				focus.right = label.right + FocusInset;
			else
				focus.left = label.left - FocusInset;
		}
		label = FocusTextBounds(label, focus);
	}
	UINT flags = DT_VCENTER | ((style & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE | DT_END_ELLIPSIS);
	if (push) flags |= DT_CENTER;
	if (SendMessage(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) flags |= DT_HIDEPREFIX;
	DrawText(dc, text, -1, &label, flags);
	if (!group && GetFocus() == window && !(SendMessage(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
	{
		PaintFocusRect(dc, focus);
	}
	SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
	RestoreDC(dc, saved);
}

void PaintTabs(HWND window, HDC dc)
{
	const int saved = SaveDC(dc);
	RECT client; GetClientRect(window, &client);
	FillRect(dc, &client, Brush(dc, Background));
	SelectObject(dc, reinterpret_cast<HFONT>(SendMessage(window, WM_GETFONT, 0, 0)));
	SetBkMode(dc, TRANSPARENT); SetTextColor(dc, Text);
	POINT cursor; GetCursorPos(&cursor); ScreenToClient(window, &cursor);
	for (int i = 0; i < TabCtrl_GetItemCount(window); ++i)
	{
		RECT rc; if (!TabCtrl_GetItemRect(window, i, &rc)) continue;
		wchar_t text[256] = {}; TCITEM item = {};
		item.mask = TCIF_TEXT; item.pszText = text; item.cchTextMax = countof(text);
		TabCtrl_GetItem(window, i, &item);
		bool selected = i == TabCtrl_GetCurSel(window);
		FillRect(dc, &rc, Brush(dc, selected ? Hover : PtInRect(&rc,cursor) ? Control : Background));
		if (selected)
		{
			RECT line = rc; line.right = line.left + 2; FillRect(dc, &line, Brush(dc, Accent));
		}
		RECT focus = rc;
		InflateRect(&focus, -FocusInset, 0);
		auto label = FocusTextBounds(rc, focus);
		DrawText(dc, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		if (selected && GetFocus() == window && !(SendMessage(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
		{
			PaintFocusRect(dc, focus);
		}
	}
	RestoreDC(dc, saved);
}

bool CustomStatic(HWND control)
{
	const int id = GetDlgCtrlID(control);
	return (id >= c0 && id <= CSetDlgColors::MAX_COLOR_EDT_ID) || (id >= c32 && id <= c38)
		|| CDlgItemHelper::isHyperlinkCtrl(id);
}

constexpr wchar_t HeaderHoverProperty[] = L"ConEmuDarkHeaderHover";

void PaintHeader(HWND window, HDC dc)
{
	const int saved = SaveDC(dc);
	RECT client; GetClientRect(window, &client);
	FillRect(dc, &client, Brush(dc, Background));
	SelectObject(dc, reinterpret_cast<HFONT>(SendMessage(window, WM_GETFONT, 0, 0)));
	SetBkMode(dc, TRANSPARENT); SetTextColor(dc, Text);
	const auto hovered = reinterpret_cast<INT_PTR>(GetProp(window, HeaderHoverProperty));
	for (int i = 0; i < Header_GetItemCount(window); ++i)
	{
		RECT rc{};
		if (!Header_GetItemRect(window, i, &rc)) continue;
		wchar_t text[256] = {}; HDITEM item = {};
		item.mask = HDI_TEXT | HDI_FORMAT; item.pszText = text; item.cchTextMax = countof(text);
		Header_GetItem(window, i, &item);
		FillRect(dc, &rc, Brush(dc, hovered == i + 1 ? Hover : Control));
		RECT edge = rc; edge.left = edge.right - 1; FillRect(dc, &edge, Brush(dc, Border));
		InflateRect(&rc, -5, 0);
		DrawText(dc, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX
			| ((item.fmt & HDF_CENTER) ? DT_CENTER : (item.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT));
	}
	RestoreDC(dc, saved);
}

void PaintBufferedHeader(HWND window, HDC dc)
{
	RECT client{};
	GetClientRect(window, &client);
	const int width = RectWidth(client), height = RectHeight(client);
	const HDC buffer = CreateCompatibleDC(dc);
	const HBITMAP bitmap = buffer && width > 0 && height > 0
		? CreateCompatibleBitmap(dc, width, height) : nullptr;
	if (bitmap)
	{
		const auto previous = SelectObject(buffer, bitmap);
		PaintHeader(window, buffer);
		BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
		SelectObject(buffer, previous);
		DeleteObject(bitmap);
	}
	else
	{
		PaintHeader(window, dc);
	}
	if (buffer) DeleteDC(buffer);
}

void UpdateHeaderHover(HWND window, UINT message, LPARAM mousePosition)
{
	const auto previous = reinterpret_cast<INT_PTR>(GetProp(window, HeaderHoverProperty));
	INT_PTR current = 0;
	if (message == WM_MOUSEMOVE)
	{
		const POINT cursor{static_cast<short>(LOWORD(mousePosition)), static_cast<short>(HIWORD(mousePosition))};
		RECT client{};
		GetClientRect(window, &client);
		if (PtInRect(&client, cursor))
		{
			for (int i = 0; i < Header_GetItemCount(window); ++i)
			{
				RECT rect{};
				if (Header_GetItemRect(window, i, &rect) && PtInRect(&rect, cursor))
				{
					current = i + 1;
					break;
				}
			}
		}
	}
	if (current == previous) return;
	if (current)
		SetProp(window, HeaderHoverProperty, reinterpret_cast<HANDLE>(current));
	else
		RemoveProp(window, HeaderHoverProperty);
	for (const auto item : {previous, current})
	{
		RECT rect{};
		if (item && Header_GetItemRect(window, static_cast<int>(item - 1), &rect))
			InvalidateRect(window, &rect, FALSE);
	}
}

void PaintLabel(HWND window, HDC dc, bool hotkey)
{
	const int saved = SaveDC(dc);
	RECT rc; GetClientRect(window, &rc);
	FillRect(dc, &rc, Brush(dc, hotkey ? Control : Background));
	HFONT font = reinterpret_cast<HFONT>(SendMessage(window, WM_GETFONT, 0, 0));
	if (!font) font = reinterpret_cast<HFONT>(SendMessage(GetParent(window), WM_GETFONT, 0, 0));
	SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
	SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(window) ? Text : Hint);
	const DWORD style = GetWindowLong(window, GWL_STYLE);
	wchar_t text[1024] = {}; GetWindowText(window, text, countof(text));
	UINT flags = DT_WORDBREAK;
	if (hotkey)
	{
		const WORD key = LOWORD(SendMessage(window, HKM_GETHOTKEY, 0, 0));
		text[0] = 0;
		if (HIBYTE(key) & HOTKEYF_CONTROL) lstrcat(text, L"Ctrl + ");
		if (HIBYTE(key) & HOTKEYF_ALT) lstrcat(text, L"Alt + ");
		if (HIBYTE(key) & HOTKEYF_SHIFT) lstrcat(text, L"Shift + ");
		if (LOBYTE(key))
		{
			const LONG scan = MapVirtualKey(LOBYTE(key), MAPVK_VK_TO_VSC) << 16;
			GetKeyNameText(scan | ((HIBYTE(key) & HOTKEYF_EXT) ? (1 << 24) : 0), text + lstrlen(text), 128);
		}
		else lstrcpy(text, L"None");
		InflateRect(&rc, -2, 0); flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
	}
	else
	{
		const DWORD type = style & SS_TYPEMASK;
		if (type == SS_CENTER) flags |= DT_CENTER;
		else if (type == SS_RIGHT) flags |= DT_RIGHT;
		if (style & SS_NOPREFIX) flags |= DT_NOPREFIX;
		if (style & SS_CENTERIMAGE) flags = (flags & ~DT_WORDBREAK) | DT_SINGLELINE | DT_VCENTER;
	}
	DrawText(dc, text, -1, &rc, flags);
	RestoreDC(dc, saved);
}

void PaintListFrame(HWND window)
{
	const auto style = GetWindowLong(window, GWL_STYLE);
	const auto exStyle = GetWindowLong(window, GWL_EXSTYLE);
	if (!(style & WS_BORDER) && !(exStyle & WS_EX_CLIENTEDGE)) return;

	RECT frame{};
	if (!GetWindowRect(window, &frame)) return;
	OffsetRect(&frame, -frame.left, -frame.top);
	if (const auto dc = GetWindowDC(window))
	{
		FrameRect(dc, &frame, Brush(dc, Border));
		if (exStyle & WS_EX_CLIENTEDGE)
		{
			InflateRect(&frame, -1, -1);
			FrameRect(dc, &frame, Brush(dc, Control));
		}
		ReleaseDC(window, dc);
	}
}

LRESULT CALLBACK ControlProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR kind)
{
	if (message == WM_NCDESTROY)
	{
		if (kind == 3) RemoveProp(window, HeaderHoverProperty);
		RemoveWindowSubclass(window, ControlProc, id);
	}
	if (global::g_darkModeEnabled)
	{
		// Enabled statics may have application painting (e.g. the palette preview).
		// Only replace the native embossed text used by disabled labels.
		const bool paint = kind && (kind != 4 || !IsWindowEnabled(window));
		if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC
			&& !(message == WM_CTLCOLORSTATIC && CustomStatic(reinterpret_cast<HWND>(lp))))
			return reinterpret_cast<LRESULT>(ControlColor(reinterpret_cast<HWND>(lp), reinterpret_cast<HDC>(wp), message));
		if (paint && (message == WM_PAINT || message == WM_PRINTCLIENT))
		{
			PAINTSTRUCT ps = {};
			HDC dc = message == WM_PAINT ? BeginPaint(window, &ps) : reinterpret_cast<HDC>(wp);
			if (kind == 1) PaintButton(window, dc);
			else if (kind == 2) PaintTabs(window, dc);
			else if (kind == 3) PaintBufferedHeader(window, dc);
			else PaintLabel(window, dc, kind == 5);
			if (message == WM_PAINT) EndPaint(window, &ps);
			return 0;
		}
		if (paint && message == WM_ERASEBKGND) return 1;
		if (kind && message == WM_MOUSEMOVE)
		{
			TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
		}
		if (kind == 3 && (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE))
			UpdateHeaderHover(window, message, lp);
	}
	const LRESULT result = DefSubclassProc(window, message, wp, lp);
	if (message == WM_NCPAINT && global::g_darkModeEnabled
		&& (IsClass(window, WC_LISTBOX) || IsClass(window, WC_LISTVIEW) || IsClass(window, WC_TREEVIEW)))
		PaintListFrame(window);
	if (kind == 3 && (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE))
		return result;
	if (kind && (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || message == WM_LBUTTONDOWN
		|| message == WM_LBUTTONUP || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_ENABLE
		|| message == BM_SETCHECK || message == BM_SETSTATE || message == WM_SETFOCUS || message == WM_KILLFOCUS))
		InvalidateRect(window, nullptr, FALSE);
	return result;
}

BOOL CALLBACK ApplyControl(HWND window, LPARAM)
{
	if (IsClass(window, L"#32770"))
	{
		SetWindowSubclass(window, ControlProc, SubclassId, 0);
		return TRUE;
	}
	const bool dark = global::g_darkModeEnabled;
	AllowDarkModeForWindow(window, dark);
	DWORD_PTR kind = 0;
	if (IsClass(window, L"Button"))
	{
		const DWORD style = GetWindowLong(window, GWL_STYLE);
		if ((style & BS_TYPEMASK) != BS_OWNERDRAW && !(style & (BS_BITMAP | BS_ICON))) kind = 1;
	}
	else if (IsClass(window, WC_TABCONTROL)) kind = 2;
	else if (IsClass(window, WC_HEADER)) kind = 3;
	else if (IsClass(window, L"Static") && (GetWindowLong(window, GWL_STYLE) & SS_TYPEMASK) <= SS_RIGHT
		&& !CustomStatic(window)) kind = 4;
	else if (IsClass(window, HOTKEY_CLASS)) kind = 5;
	SetWindowSubclass(window, ControlProc, SubclassId, kind);
	const bool edit = IsClass(window, L"Edit");
	const wchar_t* theme = nullptr;
	if (dark)
	{
		theme = edit || IsClass(window, L"ComboBox") ? L"DarkMode_CFD" : L"DarkMode_Explorer";
		if (gOSVer.dwBuildNumber >= 26300 && (IsClass(window, L"ListBox")
			|| (edit && (GetWindowLong(window, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)))))
			theme = L"DarkMode_DarkTheme";
	}
	gpConEmu->SetWindowTheme(window, theme, nullptr);
	if (IsClass(window, WC_TREEVIEW))
	{
		TreeView_SetBkColor(window, dark ? Control : GetSysColor(COLOR_WINDOW));
		TreeView_SetTextColor(window, dark ? Text : GetSysColor(COLOR_WINDOWTEXT));
		TreeView_SetLineColor(window, dark ? Border : CLR_DEFAULT);
	}
	if (IsClass(window, WC_LISTVIEW))
	{
		ListView_SetBkColor(window, dark ? Control : GetSysColor(COLOR_WINDOW));
		ListView_SetTextBkColor(window, dark ? Control : GetSysColor(COLOR_WINDOW));
		ListView_SetTextColor(window, dark ? Text : GetSysColor(COLOR_WINDOWTEXT));
	}
	RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
	return TRUE;
}
}

HBRUSH ControlColor(HWND control, HDC dc, UINT message)
{
	if (!global::g_darkModeEnabled) return nullptr;
	const bool edit = IsClass(control, L"Edit") || IsClass(control, L"ListBox") || IsClass(control, L"ComboBox");
	const COLORREF background = edit ? Control : Background;
	SetTextColor(dc, IsWindowEnabled(control) ? Text : Hint);
	SetBkColor(dc, background);
	return Brush(dc, background);
}

bool GetConsoleChromeColors(COLORREF& background, COLORREF& foreground)
{
	if (!global::g_darkModeEnabled)
		return false;

	CVConGuard active;
	if (gpConEmu->GetActiveVCon(&active) < 0 || !active->RCon())
		return false;

	const bool fade = gpSet->isFadeInactive && !gpConEmu->InQuakeAnimation()
		&& !gpConEmu->isMeForeground(true);
	const auto colors = active->GetColors(fade);
	background = colors[active->RCon()->GetDefaultBackColorIdx()];

	// Use the preferred text color when it is legible on this palette.
	const auto luminance = [](COLORREF color) {
		const auto linear = [](double channel) {
			channel /= 255.0;
			return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
		};
		return 0.2126 * linear(GetRValue(color)) + 0.7152 * linear(GetGValue(color))
			+ 0.0722 * linear(GetBValue(color));
	};
	const double bg = luminance(background);
	foreground = colors[active->RCon()->GetDefaultTextColorIdx()];
	const double fg = luminance(foreground);
	if ((std::max(bg, fg) + 0.05) / (std::min(bg, fg) + 0.05) < 4.5)
		foreground = bg >= 0.179 ? RGB(0, 0, 0) : RGB(255, 255, 255);
	return true;
}

void ApplyDialog(HWND dialog)
{
	if (!global::g_darkModeSupported) return;
	gpConEmu->RefreshTitleBarThemeColor(dialog);
	EnumChildWindows(dialog, ApplyControl, 0);
	RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

bool ProcessDialogMessage(HWND dialog, UINT message, WPARAM wp, LPARAM lp, INT_PTR& result)
{
	if (message == WM_THEMECHANGED || (message == WM_SETTINGCHANGE
		&& (IsColorSchemeChangeMessage(lp) || wp == SPI_SETHIGHCONTRAST)))
		ApplyDialog(dialog);
	if (global::g_darkModeEnabled && (message == WM_CTLCOLORDLG || message == WM_CTLCOLOREDIT
		|| message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN
		|| (message == WM_CTLCOLORSTATIC && !CustomStatic(reinterpret_cast<HWND>(lp)))))
	{
		result = reinterpret_cast<INT_PTR>(ControlColor(reinterpret_cast<HWND>(lp), reinterpret_cast<HDC>(wp), message));
		return true;
	}
	return false;
}
}
