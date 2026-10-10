#include "Tray.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

const UINT WM_TRAY = WM_APP + 1;
const UINT_PTR QUIT_TIMER = 1;
enum Command { OPEN = 1, AUTOSTART, QUIT };

const wchar_t *const APP_NAME = L"Sync Analyzer";
const wchar_t *const RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring g_url;
std::atomic<bool> *g_quit = nullptr;
UINT g_taskbarCreated = 0;
NOTIFYICONDATAW g_icon = {};

std::wstring widen(const std::string &text)
{
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	std::wstring wide(size > 0 ? size - 1 : 0, L'\0');
	if (size > 1) {
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], size);
	}
	return wide;
}

void openUi()
{
	ShellExecuteW(nullptr, L"open", g_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Started with Windows: a value in the user's Run key, starting this program without a browser.
bool autostart()
{
	return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void setAutostart(bool enable)
{
	HKEY key;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
		return;
	}
	if (enable) {
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		const std::wstring command = L"\"" + std::wstring(path) + L"\" --no-browser";
		RegSetValueExW(key, APP_NAME, 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()),
			DWORD((command.size() + 1) * sizeof(wchar_t)));
	} else {
		RegDeleteValueW(key, APP_NAME);
	}
	RegCloseKey(key);
}

void showMenu(HWND window)
{
	HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING, OPEN, L"Open Sync Analyzer");
	AppendMenuW(menu, MF_STRING | (autostart() ? MF_CHECKED : MF_UNCHECKED), AUTOSTART, L"Start with Windows");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, QUIT, L"Quit");
	SetMenuDefaultItem(menu, OPEN, FALSE);
	POINT point;
	GetCursorPos(&point);
	// Without this the menu does not close when clicking elsewhere.
	SetForegroundWindow(window);
	const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
	DestroyMenu(menu);
	switch (command) {
	case OPEN:
		openUi();
		break;
	case AUTOSTART:
		setAutostart(!autostart());
		break;
	case QUIT:
		*g_quit = true;
		break;
	}
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_TRAY) {
		if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
			openUi();
		} else if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
			showMenu(window);
		}
		return 0;
	}
	if (message == WM_TIMER && wParam == QUIT_TIMER) {
		if (*g_quit) {
			PostQuitMessage(0);
		}
		return 0;
	}
	// Explorer restarted: the icon is gone, add it again.
	if (message == g_taskbarCreated && g_taskbarCreated) {
		Shell_NotifyIconW(NIM_ADD, &g_icon);
		return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

void runTray(const std::string &url, std::atomic<bool> &quit)
{
	g_url = widen(url);
	g_quit = &quit;
	g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

	HINSTANCE instance = GetModuleHandleW(nullptr);
	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = windowProc;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = L"SyncAnalyzerTray";
	RegisterClassW(&windowClass);
	HWND window = CreateWindowW(windowClass.lpszClassName, APP_NAME, 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);

	g_icon.cbSize = sizeof(g_icon);
	g_icon.hWnd = window;
	g_icon.uID = 1;
	g_icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	g_icon.uCallbackMessage = WM_TRAY;
	// The application icon (resource 1), else the default one.
	g_icon.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
	if (!g_icon.hIcon) {
		g_icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
	}
	const std::wstring tip = std::wstring(APP_NAME) + L" - " + g_url;
	wcsncpy_s(g_icon.szTip, tip.c_str(), _TRUNCATE);
	Shell_NotifyIconW(NIM_ADD, &g_icon);

	// The server may stop by itself (or Quit was chosen): checked a few times a second.
	SetTimer(window, QUIT_TIMER, 200, nullptr);
	MSG message;
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	Shell_NotifyIconW(NIM_DELETE, &g_icon);
	DestroyWindow(window);
	quit = true;
}

void showTrayError(const std::string &message)
{
	MessageBoxW(nullptr, widen(message).c_str(), APP_NAME, MB_OK | MB_ICONERROR);
}
