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

//UIAutomation 实例，用来区分任务栏的空白处和应用图标
IUIAutomation* g_pUIAutomation = NULL;

//初始化 UIAutomation，重复调用无副作用
void InitUIAutomation()
{
	if (g_pUIAutomation) return;

	HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	//S_OK/S_FALSE 表示成功；RPC_E_CHANGED_MODE 表示本线程已按其它套间初始化，COM 依然可用
	if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) return;

	CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&g_pUIAutomation));
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
//**************************************************
void SetMouseHook(DWORD threadId)
{
	InitUIAutomation();

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