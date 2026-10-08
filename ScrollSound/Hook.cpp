#include "Hook.h"
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>

#pragma comment(lib, "uiautomationcore.lib")

HHOOK mouseHook;
HWND windowHwnd;
POINT p;
TCHAR taskBarClassName[100];
CWinVersionHelper cwvh;

MSLLHOOKSTRUCT* mWheelInfo;	
bool down = false;			//滚轮是否向下滚动

MSLLHOOKSTRUCT* mButtonDownInfo;
TCHAR winClassName[100];

extern HWND hWnd;							//主窗口句柄，定义在 ScrollSound.cpp

//UIAutomation 实例，用来区分任务栏的空白处和应用图标
IUIAutomation* g_pUIAutomation = NULL;

//双击任务栏空白处要执行的动作，来自 setting.ini
static bool g_dblEnabled = true;
static TCHAR g_dblCmd[MAX_PATH] = { 0 };
static TCHAR g_dblArgs[MAX_PATH] = { 0 };

//双击判定参数（来自 setting.ini）：0 表示跟随系统设置
static int g_dblIntervalCfg = 0;			//两次点击允许的最大间隔，单位毫秒
static int g_dblRectCfg = 0;				//两次点击允许的最大偏移，单位像素

//真正生效的判定参数：读配置时一次算好，鼠标钩子回调里只做比较，
//避免在钩子里多做 GetDoubleClickTime / GetSystemMetrics 这类系统调用
static int g_dblIntervalMs = 500;
static int g_dblRectX = 2;
static int g_dblRectY = 2;

//上次读取配置时 setting.ini 的修改时间，用来实现配置热更新
static FILETIME g_iniWriteTime = { 0, 0 };

//双击判定状态：记录上一次左键按下的时间和位置
static bool g_hasLastClick = false;			//是否已有待配对的首击（不用时间戳是否为 0 判断，避免计时器回绕边界）
static DWORD g_lastClickTick = 0;
static POINT g_lastClickPos = { 0, 0 };
static POINT g_dblFirstPoint = { 0, 0 };	//判定为双击时第一击的屏幕坐标
static POINT g_dblClickPoint = { 0, 0 };	//判定为双击时第二击的屏幕坐标

//把带 UTF-8 BOM 的 setting.ini 转成 UTF-16LE（内容不变）
static void ConvertIniUtf8ToUtf16()
{
	HANDLE hFile = CreateFile(SETTING_PATH, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return;

	DWORD size = GetFileSize(hFile, NULL);
	if (size <= 3u || size > 1024u * 1024u) { CloseHandle(hFile); return; }	//大小异常则不改动

	std::vector<BYTE> buf(size);
	DWORD read = 0;
	BOOL ok = ReadFile(hFile, buf.data(), size, &read, NULL);
	CloseHandle(hFile);
	if (!ok || read <= 3u) return;

	int wlen = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)buf.data() + 3, (int)read - 3, NULL, 0);
	if (wlen <= 0) return;

	std::wstring text(wlen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)buf.data() + 3, (int)read - 3, &text[0], wlen);

	//先写临时文件再替换：直接对原文件 CREATE_ALWAYS 会先清空它，中途失败就只剩半截配置
	std::wstring tmpPath(SETTING_PATH);
	tmpPath += _T(".tmp");

	hFile = CreateFile(tmpPath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return;

	const BYTE bom[2] = { 0xFF, 0xFE };
	DWORD written = 0;
	DWORD want = (DWORD)(text.size() * sizeof(WCHAR));
	BOOL wroteBom = WriteFile(hFile, bom, 2, &written, NULL);
	BOOL wroteText = WriteFile(hFile, text.data(), want, &written, NULL);
	CloseHandle(hFile);

	if (wroteBom && wroteText && written == want) {
		MoveFileEx(tmpPath.c_str(), SETTING_PATH, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	}
	else {
		DeleteFile(tmpPath.c_str());
	}
}

//保证 setting.ini 是 UTF-16LE（带 BOM）：
//文件不存在时新建，带 UTF-8 BOM 时转换，其它情况保持原样。
//原因是 GetPrivateProfileStringW 只把带 UTF-16 BOM 的 ini 当 Unicode 解析，
//UTF-8 会被当成 ANSI，含中文的路径会变成乱码。
void EnsureSettingFileUnicode()
{
	HANDLE hFile = CreateFile(SETTING_PATH, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		//文件不存在：新建并写入 UTF-16LE 的 BOM，其余内容交给 profile API
		hFile = CreateFile(SETTING_PATH, GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			const BYTE bom[2] = { 0xFF, 0xFE };
			DWORD written = 0;
			WriteFile(hFile, bom, 2, &written, NULL);
			CloseHandle(hFile);
		}
		return;
	}

	BYTE head[3] = { 0 };
	DWORD read = 0;
	ReadFile(hFile, head, 3, &read, NULL);
	CloseHandle(hFile);

	//带 UTF-8 BOM 的文件会被 profile API 当成 ANSI 解析，这里转成 UTF-16LE
	if (read >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
		ConvertIniUtf8ToUtf16();
	}
}

//判断 ini 里某个键是否存在：读不到的键会返回这里给的哨兵默认值
static bool IniKeyExists(LPCTSTR key)
{
	TCHAR probe[2] = { 0 };
	GetPrivateProfileString(SECTION_NAME, key, _T("\x01"), probe, 2, SETTING_PATH);
	return probe[0] != _T('\x01');
}

//读取 ini 里的整数判定参数：
//键不存在时写入默认值；配 0 或负数表示跟随系统设置；其余值夹到 [minVal, maxVal]，
//避免手写的越界值让判定变得过宽（比如 100000 毫秒）或者直接失效（比如 0 被当成不判定）
static int ReadIniIntClamped(LPCTSTR key, int defaultValue, int minVal, int maxVal)
{
	if (!IniKeyExists(key)) {
		TCHAR def[16] = { 0 };
		_stprintf_s(def, _T("%d"), defaultValue);
		WritePrivateProfileString(SECTION_NAME, key, def, SETTING_PATH);
		return defaultValue;
	}

	int value = GetPrivateProfileInt(SECTION_NAME, key, defaultValue, SETTING_PATH);
	if (value <= 0) return 0;				//0 表示跟随系统设置
	if (value < minVal) return minVal;
	if (value > maxVal) return maxVal;
	return value;
}

//读取 setting.ini 里的双击动作配置；缺少的配置项写入默认值（默认动作是任务管理器）
static void LoadDoubleClickSetting()
{
	if (!IniKeyExists(KEY_DBLCLICK_CMD))
		WritePrivateProfileString(SECTION_NAME, KEY_DBLCLICK_CMD, _T("taskmgr.exe"), SETTING_PATH);
	if (!IniKeyExists(KEY_DBLCLICK_ENABLE))
		WritePrivateProfileString(SECTION_NAME, KEY_DBLCLICK_ENABLE, _T("1"), SETTING_PATH);
	if (!IniKeyExists(KEY_DBLCLICK_ARGS))
		WritePrivateProfileString(SECTION_NAME, KEY_DBLCLICK_ARGS, _T(""), SETTING_PATH);

	//命令留空即表示不执行任何动作
	GetPrivateProfileString(SECTION_NAME, KEY_DBLCLICK_CMD, _T(""), g_dblCmd, MAX_PATH, SETTING_PATH);
	GetPrivateProfileString(SECTION_NAME, KEY_DBLCLICK_ARGS, _T(""), g_dblArgs, MAX_PATH, SETTING_PATH);
	g_dblEnabled = GetPrivateProfileInt(SECTION_NAME, KEY_DBLCLICK_ENABLE, 1, SETTING_PATH) != 0;

	//判定参数：默认 300ms（比系统的 500ms 严格），判定距离默认跟随系统
	g_dblIntervalCfg = ReadIniIntClamped(KEY_DBLCLICK_INTERVAL, DBLCLICK_INTERVAL_DEFAULT,
		DBLCLICK_INTERVAL_MIN, DBLCLICK_INTERVAL_MAX);
	g_dblRectCfg = ReadIniIntClamped(KEY_DBLCLICK_RECT, 0, DBLCLICK_RECT_MIN, DBLCLICK_RECT_MAX);

	//算出真正生效的值：配置为 0 时取系统设置；X/Y 分别取，避免系统宽高不等时判定失真
	g_dblIntervalMs = (g_dblIntervalCfg > 0) ? g_dblIntervalCfg : static_cast<int>(GetDoubleClickTime());
	g_dblRectX = (g_dblRectCfg > 0) ? g_dblRectCfg : GetSystemMetrics(SM_CXDOUBLECLK) / 2;
	g_dblRectY = (g_dblRectCfg > 0) ? g_dblRectCfg : GetSystemMetrics(SM_CYDOUBLECLK) / 2;
}

//取 setting.ini 的最后修改时间，用来判断配置有没有被改过
static bool GetSettingWriteTime(FILETIME* ft)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(SETTING_PATH, GetFileExInfoStandard, &fad)) return false;
	*ft = fad.ftLastWriteTime;
	return true;
}

//立即重读配置并生效：改完 setting.ini 不用重启，也不用重新 hook
void ReloadDoubleClickSetting()
{
	//profile API 会缓存 ini 内容，外部编辑器改过的值可能读不到；
	//按 MSDN 的做法用一次空写入把缓存刷掉，保证读到磁盘上的最新内容
	WritePrivateProfileString(NULL, NULL, NULL, SETTING_PATH);

	LoadDoubleClickSetting();

	//LoadDoubleClickSetting 在缺键时会补写默认值，这里必须重新取一次文件时间，
	//否则记录值永远落后于文件，监视定时器会每秒重读一次
	GetSettingWriteTime(&g_iniWriteTime);
}

//配置监视定时器：每秒看一眼 setting.ini 有没有被改动，改了就地重读，实现热更新。
//运行在主线程的消息泵里，和鼠标钩子回调同线程，因此判定参数不需要额外同步
static void CALLBACK DoubleClickConfigWatchProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	FILETIME ft;
	if (!GetSettingWriteTime(&ft)) return;

	if (ft.dwLowDateTime == g_iniWriteTime.dwLowDateTime
		&& ft.dwHighDateTime == g_iniWriteTime.dwHighDateTime) return;

	ReloadDoubleClickSetting();
}

//启动配置监视定时器（程序启动时调用一次即可）
void StartDoubleClickConfigWatch()
{
	static UINT_PTR watchTimer = 0;
	if (watchTimer) return;

	//先记下当前文件时间做基线，避免启动时立刻触发一次无意义的重读
	if (g_iniWriteTime.dwLowDateTime == 0 && g_iniWriteTime.dwHighDateTime == 0)
		GetSettingWriteTime(&g_iniWriteTime);

	watchTimer = SetTimer(NULL, 0, DBLCLICK_CONFIG_WATCH_MS, DoubleClickConfigWatchProc);
}

//给托盘菜单用：返回 ini 里配的判定时间（毫秒），0 表示跟随系统
int GetDoubleClickIntervalSetting()
{
	return g_dblIntervalCfg;
}

//给托盘菜单用：写入判定时间并立即生效（0 表示跟随系统）
void SetDoubleClickIntervalSetting(int ms)
{
	TCHAR text[16] = { 0 };
	_stprintf_s(text, _T("%d"), (ms > 0 ? ms : 0));
	WritePrivateProfileString(SECTION_NAME, KEY_DBLCLICK_INTERVAL, text, SETTING_PATH);

	ReloadDoubleClickSetting();
}

//初始化 UIAutomation，重复调用无副作用
void InitUIAutomation()
{
	if (g_pUIAutomation) return;

	HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	//S_OK/S_FALSE 表示成功；RPC_E_CHANGED_MODE 表示本线程已按其它套间初始化，COM 依然可用
	if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) {
		OutputDebugString(_T("ScrollSound: CoInitializeEx failed, UIAutomation disabled\n"));
		return;
	}

	HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&g_pUIAutomation));
	if (FAILED(hr) || !g_pUIAutomation) {
		//UIAutomation 不可用时双击和中键静音都会静默失效，留一条调试输出便于排查
		g_pUIAutomation = NULL;
		OutputDebugString(_T("ScrollSound: UIAutomation unavailable, double-click and mute disabled\n"));
	}
}

//判断点是否落在任务栏的空白处（落在应用图标、按钮上时返回 false）
//WindowFromPoint 只能取到任务栏的父窗口，区分不了空白和图标，所以必须借助 UIAutomation：
//ElementFromPoint 返回的是该点下最具体的元素，点在图标的按钮上会返回按钮元素，点在真空白处才返回任务栏容器本身
bool IsTaskbarEmptySpace(POINT ptScreen)
{
	if (!g_pUIAutomation) return false;

	IUIAutomationElement* pElement = NULL;
	if (FAILED(g_pUIAutomation->ElementFromPoint(ptScreen, &pElement)) || !pElement)
		return false;

	bool isEmpty = false;
	BSTR className = NULL;
	if (SUCCEEDED(pElement->get_CurrentClassName(&className)) && className) {
		isEmpty = (wcscmp(className, L"Shell_TrayWnd") == 0)						//Win10 主任务栏
			|| (wcscmp(className, L"Shell_SecondaryTrayWnd") == 0)					//Win10 副任务栏
			|| (wcscmp(className, L"Taskbar.TaskbarFrameAutomationPeer") == 0)		//Win11
			|| (wcscmp(className, L"Windows.UI.Input.InputSite.WindowClass") == 0);	//Win11 21H2
		SysFreeString(className);
	}
	pElement->Release();
	return isEmpty;
}

//用窗口类名做一次廉价过滤：判断这个点是不是落在任务栏上（不做 UIA 查询）
static bool IsOnTaskbar(POINT ptScreen)
{
	HWND hPointWnd = WindowFromPoint(ptScreen);
	if (!hPointWnd) return false;

	TCHAR cls[100] = { 0 };
	GetClassName(hPointWnd, cls, 100);
	return _tcscmp(cls, taskBarClassName) == 0;
}

//执行双击任务栏空白处的动作。
//由主消息循环调用，避免 UIA 查询和启动进程拖慢鼠标钩子。
void RunTaskbarDoubleClickAction()
{
	if (!g_dblEnabled || g_dblCmd[0] == 0) return;

	//两次点击都必须落在任务栏空白处，否则“先点任务栏图标、紧接着点旁边空白处”会被误判成双击
	if (!IsOnTaskbar(g_dblFirstPoint) || !IsOnTaskbar(g_dblClickPoint)) return;
	if (!IsTaskbarEmptySpace(g_dblFirstPoint) || !IsTaskbarEmptySpace(g_dblClickPoint)) return;

	//把工作目录设成 exe 所在目录，这样配置里写相对路径也能用
	TCHAR workDir[MAX_PATH] = { 0 };
	_tcscpy_s(workDir, SETTING_PATH);
	TCHAR* slash = _tcsrchr(workDir, _T('\\'));
	if (slash) *slash = _T('\0');

	HINSTANCE hRet = ShellExecute(NULL, _T("open"), g_dblCmd,
		(g_dblArgs[0] != 0 ? g_dblArgs : NULL),
		(workDir[0] != 0 ? workDir : NULL), SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(hRet) <= 32) {
		//配置的命令打不开时退回任务管理器，避免功能静默失效
		OutputDebugString(_T("ScrollSound: DoubleClickCommand failed, fallback to taskmgr.exe\n"));
		ShellExecute(NULL, _T("open"), _T("taskmgr.exe"), NULL, workDir, SW_SHOWNORMAL);
	}
}

LRESULT CALLBACK MouseProc(
	_In_ int code,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam)
{

	switch (wParam)
	{
	case WM_MOUSEWHEEL:
	{
		GetCursorPos(&p);
		windowHwnd = WindowFromPoint(p);
		GetClassName(windowHwnd, winClassName, 100);
		mWheelInfo = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

		//std::wcout << "类名：" << winClassName << endl;
		down = static_cast<std::make_signed_t<WORD>>(HIWORD(mWheelInfo->mouseData)) < 0;
		if (down && _tcscmp(winClassName, taskBarClassName) == 0) {
			PostMessage(windowHwnd, WM_APPCOMMAND, 0, APPCOMMAND_VOLUME_DOWN << 16);

		}
		else if (_tcscmp(winClassName, taskBarClassName) == 0) {
			PostMessage(windowHwnd, WM_APPCOMMAND, 0, APPCOMMAND_VOLUME_UP << 16);
		}
		break;
	}


	case WM_MBUTTONDOWN:
	{
		GetCursorPos(&p);
		windowHwnd = WindowFromPoint(p);
		GetClassName(windowHwnd, winClassName, 100);

		//解决和其他的全局钩子冲突,如：会和wgesture冲突，触发两次WM_MBUTTONDOWN
		mButtonDownInfo = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
		if (mButtonDownInfo) {
			//std::cout << "flags: " << mButtonDownInfo->flags << std::endl;
			//std::cout << "dwExtraInfo: " << mButtonDownInfo->dwExtraInfo << std::endl;
			//加上 IsTaskbarEmptySpace 是为了排除任务栏上的应用图标：
			//中键点图标是"新建窗口"，不该被静音
			if (mButtonDownInfo->dwExtraInfo == 0
				&& _tcscmp(winClassName, taskBarClassName) == 0
				&& IsTaskbarEmptySpace(p)) {
				PostMessage(windowHwnd, WM_APPCOMMAND, 0, APPCOMMAND_VOLUME_MUTE << 16);
				break;
			}		
		}
		break;								//显式跳出，避免落入后面的 case
	}

	case WM_LBUTTONDOWN:
	{
		//双击任务栏空白处：不能依赖 WM_LBUTTONDBLCLK（任务栏窗口类不一定带 CS_DBLCLKS 样式），
		//这里用双击判定参数自己判定：间隔和偏移都来自 setting.ini，可热更新，配 0 则跟随系统设置。
		//钩子里只做取坐标和比较这类最轻量的操作，是否在任务栏、是不是空白处都交给主消息循环，
		//避免在钩子回调里做 WindowFromPoint / UIA 查询导致超时被系统摘掉钩子。
		GetCursorPos(&p);

		DWORD now = GetTickCount();
		bool isDoubleClick = g_hasLastClick
			&& (now - g_lastClickTick <= static_cast<DWORD>(g_dblIntervalMs))
			&& (abs(static_cast<int>(p.x - g_lastClickPos.x)) <= g_dblRectX)
			&& (abs(static_cast<int>(p.y - g_lastClickPos.y)) <= g_dblRectY);

		POINT firstPos = g_lastClickPos;
		g_hasLastClick = true;
		g_lastClickTick = now;
		g_lastClickPos = p;

		if (isDoubleClick) {
			g_hasLastClick = false;			//清空，避免三击重复触发
			g_dblFirstPoint = firstPos;
			g_dblClickPoint = p;
			//交给主消息循环去判断是不是任务栏空白处并启动程序
			if (hWnd) PostMessage(hWnd, WM_DBLCLICK_TASKBAR, 0, 0);
		}
		break;								//不拦截消息，任务栏单击行为不变
	}

	}


	return CallNextHookEx(NULL, code, wParam, lParam); //第一个参数一般可以为NULL
}


//**************************************************
//	函数：SetMouseHook()
//
//	功能： 监听全局鼠标
//
//	参数：threadId为需要hook的线程ID,0为hook全局；
//
//	返回值：void
// SetWindowsHookEx 函数会将一个钩子安装在钩子链的头部。
// 当被某种钩子监视的消息出现时，系统会从钩子链的链头开始调用与该类型钩子关联的钩子回调方法。
// 钩子链中的每个钩子决定了是否将消息传递给下一个过程。
// 通过调用 CallNextHookEx，钩子过程将事件传递给下一个过程。
// 另外：鼠标在任务栏空白处双击左键时，会打开 setting.ini 里配置的程序（默认任务管理器）。
//**************************************************
void SetMouseHook(DWORD threadId)
{
	InitUIAutomation();
	ReloadDoubleClickSetting();				//重新 hook 时顺带重新读取配置（含判定参数）

	if (cwvh.IsWindows10()) {
		_tcscpy_s(taskBarClassName, _T("MSTaskListWClass"));
	}

	if (cwvh.IsWindows11_21H2()) {
		_tcscpy_s(taskBarClassName, _T("ReBarWindow32"));
	}

	if (cwvh.IsWindows11_22H2OrLater()) {
		_tcscpy_s(taskBarClassName, _T("Shell_TrayWnd"));
	}

	mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, NULL, threadId);
}

void MoveMouseHook() {
	UnhookWindowsHookEx(mouseHook);
	mouseHook = NULL;
}

void ReHook() {
	MoveMouseHook();
	SetMouseHook(0);
}