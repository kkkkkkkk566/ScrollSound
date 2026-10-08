// ScrollSound.cpp : 定义应用程序的入口点。
//editbin /SUBSYSTEM:CONSOLE "$(OUTDIR)\$(ProjectName).exe"

#include "framework.h"
#include "ScrollSound.h"
#include "Hook.h"
#include "WIC.h"
#include "AutoRunning.h"
#include "WinVersionHelper.h"
#include <thread>
#include "Privilege.h"

#define MAX_LOADSTRING 100
#define MAX_HOOKTIME 10							//运行后自动hook的最大次数

// 全局变量:
HINSTANCE hInst;                                // 当前实例
TCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
TCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名
AutoRunning aR;                                 // 开机自启对象
HWND hWnd;
NOTIFYICONDATA nid;
HMENU hMenu, subMenu;							//托盘菜单
POINT pt;
INT menuItemId;

int hookTime=0;									//hook次数
bool isPause = TRUE;							//


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	//先把 setting.ini 统一成 UTF-16LE，后面的配置读取才能正确处理中文路径
	EnsureSettingFileUnicode();

	if (IsSettingAdmin() && !IsAdmin()) {
		adminrun();
		return FALSE;
	}
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// 初始化全局字符串
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_SCROLLSOUND, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);


	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_SCROLLSOUND));

	MSG msg;

	//开始Hook
	SetMouseHook(0);

	//监视 setting.ini：双击判定参数改了就地生效，不需要重启或重新 hook
	StartDoubleClickConfigWatch();

	//设置定时器，一分钟执行一次hook，执行10次。
	//目的是为让MousHOOK保持在HOOK链的链头，以避免和其他软件冲突。
	UINT_PTR timerId = SetTimer(NULL, 0, 1000*60, TimerProc); // 非阻塞定时器
	if (!timerId) {
		//std::cerr << "Failed to create timer." << std::endl;
	}
	

	// 主消息循环:
	while (GetMessage(&msg, nullptr, 0, 0))
	{

		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

	}
	return (int)msg.wParam;
}



//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDR_MAINFRAME));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_SCROLLSOUND);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDR_MAINFRAME));

	return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // 将实例句柄存储在全局变量中
	hWnd = CreateWindowEx(WS_EX_TOOLWINDOW ,
		szWindowClass, szTitle, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, nullptr);


	if (!hWnd) {
		return FALSE;
	}

	// 初始化托盘和菜单
	InitTray(hInstance, hWnd);

	ShowWindow(hWnd, nCmdShow);//SW_HIDE
	UpdateWindow(hWnd);

	return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_DESTROY  - 发送退出消息并返回
//
//托盘菜单刷新函数定义在文件后面（InitTray 之前），这里先声明，供 WndProc 弹出菜单时调用
static void UpdateDoubleClickTimeMenu();

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &nid);
		PostQuitMessage(0);
		break;
	case WM_DBLCLICK_TASKBAR:
		RunTaskbarDoubleClickAction();
		break;
	case WM_USER:
		if (lParam == WM_RBUTTONDOWN)
		{
			GetCursorPos(&pt);//取鼠标坐标
			::SetForegroundWindow(hWnd);//解决在菜单外单击左键菜单不消失的问题
			UpdateDoubleClickTimeMenu();//弹出前刷新，保证档位勾选和标题都是最新的
			menuItemId = ::TrackPopupMenu(subMenu, TPM_RETURNCMD |TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, NULL, hWnd, NULL);//显示菜单并获取选项ID
			TrayMenuMessage(menuItemId);
			if (menuItemId == 0) PostMessage(hWnd, WM_LBUTTONDOWN, NULL, NULL);
		}
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}


//托盘菜单里的双击判定时间档位：0 表示跟随系统设置
struct DoubleClickTimePreset { UINT id; int ms; LPCTSTR text; };
static const DoubleClickTimePreset kDoubleClickTimePresets[] = {
	{ ID_DBLCLICK_TIME_150,    150, _T("150 毫秒") },
	{ ID_DBLCLICK_TIME_200,    200, _T("200 毫秒") },
	{ ID_DBLCLICK_TIME_250,    250, _T("250 毫秒") },
	{ ID_DBLCLICK_TIME_300,    300, _T("300 毫秒") },
	{ ID_DBLCLICK_TIME_400,    400, _T("400 毫秒") },
	{ ID_DBLCLICK_TIME_500,    500, _T("500 毫秒") },
	{ ID_DBLCLICK_TIME_SYSTEM, 0,   _T("跟随系统设置") },
};
static const int kDoubleClickTimePresetCount = _countof(kDoubleClickTimePresets);

//“双击判定时间”子菜单在“菜单”里的位置（0 起算，紧跟“以管理员权限运行”之后）
#define DBLCLICK_TIME_MENU_POS 3

//“双击判定时间”子菜单：标题要随配置变化，所以在代码里动态建，不写进资源脚本
static HMENU hDoubleClickTimeMenu = NULL;

//创建并插入“双击判定时间”子菜单，由 InitTray 调用一次
static void CreateDoubleClickTimeMenu()
{
	hDoubleClickTimeMenu = CreatePopupMenu();
	if (!hDoubleClickTimeMenu) return;

	for (int i = 0; i < kDoubleClickTimePresetCount; i++) {
		AppendMenu(hDoubleClickTimeMenu, MF_STRING, kDoubleClickTimePresets[i].id,
			kDoubleClickTimePresets[i].text);
	}

	InsertMenu(subMenu, DBLCLICK_TIME_MENU_POS, MF_BYPOSITION | MF_POPUP | MF_STRING,
		reinterpret_cast<UINT_PTR>(hDoubleClickTimeMenu), _T("双击判定时间(&D)"));
}

//刷新“双击判定时间”子菜单：标题里带上当前生效值，档位里勾选匹配项。
//setting.ini 里手写的非档位值不会勾中任何档位，但标题仍能看到实际生效的毫秒数
static void UpdateDoubleClickTimeMenu()
{
	if (!hDoubleClickTimeMenu) return;

	int cfgMs = GetDoubleClickIntervalSetting();
	int effectiveMs = (cfgMs > 0) ? cfgMs : static_cast<int>(GetDoubleClickTime());

	TCHAR title[64] = { 0 };
	_stprintf_s(title, _T("双击判定时间(%dms)(&D)"), effectiveMs);
	ModifyMenu(subMenu, DBLCLICK_TIME_MENU_POS, MF_BYPOSITION | MF_POPUP | MF_STRING,
		reinterpret_cast<UINT_PTR>(hDoubleClickTimeMenu), title);

	UINT checkId = ID_DBLCLICK_TIME_SYSTEM;
	bool matched = false;
	for (int i = 0; i < kDoubleClickTimePresetCount; i++) {
		if (cfgMs > 0 && kDoubleClickTimePresets[i].ms == cfgMs) {
			checkId = kDoubleClickTimePresets[i].id;
			matched = true;
			break;
		}
	}

	//勾选要作用在子菜单本身：CheckMenuRadioItem 不会递归到子菜单里去找这些 ID
	if (matched) {
		//档位 ID 连续且升序，可以直接按范围勾选
		CheckMenuRadioItem(hDoubleClickTimeMenu, kDoubleClickTimePresets[0].id,
			kDoubleClickTimePresets[kDoubleClickTimePresetCount - 1].id, checkId, MF_BYCOMMAND);
	}
	else {
		//非档位值：全部取消勾选，免得看起来像“跟随系统设置”
		for (int i = 0; i < kDoubleClickTimePresetCount; i++) {
			CheckMenuItem(hDoubleClickTimeMenu, kDoubleClickTimePresets[i].id,
				MF_BYCOMMAND | MF_UNCHECKED);
		}
	}
}

// 初始化托盘和菜单
void InitTray(HINSTANCE hInstance, HWND hWnd)
{
	nid.cbSize = sizeof(nid);
	nid.hWnd = hWnd;
	nid.uID = 0;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_USER;
	nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_TRAY_ICON));
	lstrcpy(nid.szTip, APP_NAME);


	hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_TASK_BAR_MENU));//加载资源生成菜单

	subMenu = GetSubMenu(hMenu, 0);

	CreateDoubleClickTimeMenu();//动态插入“双击判定时间”子菜单

	CMenuIcon::AddIconToMenuItem(subMenu, ID_ABOUT, FALSE, GetMenuIcon(IDI_TRAY_ICON));
	CMenuIcon::AddIconToMenuItem(subMenu, ID_APP_EXIT, FALSE, GetMenuIcon(IDI_EXIT_ICON));

	CheckMenuItem(subMenu, ID_AUTO_RUNNING, (aR.IsAutoRunning() ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(subMenu, ID_ADMIN, (IsSettingAdmin() ? MF_CHECKED : MF_UNCHECKED));
	UpdateDoubleClickTimeMenu();
	Shell_NotifyIcon(NIM_ADD, &nid);
}

// 托盘菜单点击消息处理程序。
void TrayMenuMessage(int MessageID) {
	switch (MessageID)
	{
	case ID_APP_EXIT:
		Shell_NotifyIcon(NIM_DELETE, &nid);
		MoveMouseHook();
		PostQuitMessage(0);
		break;

	case ID_AUTO_RUNNING:
		if (aR.IsAutoRunning()) {
			aR.CanclePowerOn();
		}
		else
		{
			aR.AutoStart();
		}
		CheckMenuItem(subMenu, ID_AUTO_RUNNING, (aR.IsAutoRunning() ? MF_CHECKED : MF_UNCHECKED));
		break;

	case ID_ADMIN:
		if (IsSettingAdmin()) {
			WritePrivateProfileString(SECTION_NAME, KEY_NAME, _T("0"), SETTING_PATH);
			CheckMenuItem(subMenu, ID_ADMIN, MF_UNCHECKED);
		}
		else {
			WritePrivateProfileString(SECTION_NAME, KEY_NAME, _T("1"), SETTING_PATH);
			CheckMenuItem(subMenu, ID_ADMIN, MF_CHECKED);
			if (!IsAdmin()) {
				adminrun();
				Shell_NotifyIcon(NIM_DELETE, &nid);
				PostQuitMessage(0);
			}
		}
		break;

	case ID_ABOUT:
		ShellExecute(NULL, _T("open"), _T("https://github.com/SWDaby/ScrollSound"), NULL, NULL, SW_SHOW);
		break;

	case ID_REHOOK:
		//cout << "ReHook" << endl;
		ReHook();
		MessageBoxTimeout(NULL, _T("HOOK 成功"), _T("REHOOK"), MB_ICONINFORMATION, 1000);
		ModifyMenu(subMenu, ID_PAUSE, MF_BYCOMMAND | MF_STRING, ID_PAUSE, _T("暂停"));
		isPause = TRUE;
		break;

	case ID_PAUSE:
		if (isPause) {
			MoveMouseHook();
			//cout << "MoveMouseHook" << endl;
			ModifyMenu(subMenu, ID_PAUSE, MF_BYCOMMAND | MF_STRING, ID_PAUSE, _T("继续"));
			isPause = FALSE;
		}
		else
		{
			SetMouseHook(0);
			//cout << "SetMouseHook" << endl;
			ModifyMenu(subMenu, ID_PAUSE, MF_BYCOMMAND | MF_STRING, ID_PAUSE, _T("暂停"));
			isPause = TRUE;
		}
		break;

	default:
		//双击判定时间档位：写回 setting.ini 并立即生效，同时刷新勾选
		for (int i = 0; i < kDoubleClickTimePresetCount; i++) {
			if (MessageID == static_cast<int>(kDoubleClickTimePresets[i].id)) {
				SetDoubleClickIntervalSetting(kDoubleClickTimePresets[i].ms);
				UpdateDoubleClickTimeMenu();
				break;
			}
		}
		break;
	}

}



//定时器回调
void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
	//std::cout << "Timer triggered at " << dwTime << " ms!" << std::endl;
	if (isPause) {
		ReHook();
	}
	hookTime++;
	if (hookTime == MAX_HOOKTIME) {
		KillTimer(NULL, idEvent);
	}
	
}

int MessageBoxTimeout(HWND hWnd, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType, DWORD dwMilliseconds) {
	std::thread([=]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
		HWND hMsgBox = FindWindow(NULL, lpCaption);
		if (hMsgBox) {
			PostMessage(hMsgBox, WM_CLOSE, 0, 0);
		}
		}).detach();

	return MessageBox(hWnd, lpText, lpCaption, uType);
}