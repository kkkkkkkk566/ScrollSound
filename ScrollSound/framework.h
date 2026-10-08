// header.h: 标准系统包含文件的包含文件，
// 或特定于项目的包含文件
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>
// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <iostream>
#include <string>

#include <ShlObj.h>
#include <shellapi.h>
#include <vector>
#include <Shlwapi.h>

using namespace std;


#define APP_NAME L"ScrollSound V1.24"
#define SECTION_NAME L"setting"
#define KEY_NAME  L"Administrator"

//双击任务栏空白处的动作配置项
#define KEY_DBLCLICK_ENABLE L"DoubleClickEnabled"
#define KEY_DBLCLICK_CMD    L"DoubleClickCommand"
#define KEY_DBLCLICK_ARGS   L"DoubleClickArgs"

//setting.ini 的绝对路径（exe 同目录），首次调用时计算并缓存。
//不能再用 ".\setting.ini"：开机自启时进程的当前目录未必是 exe 所在目录，那样会读不到配置。
inline const TCHAR* GetSettingPath()
{
	static TCHAR path[MAX_PATH] = { 0 };
	if (path[0] != 0) return path;

	DWORD len = GetModuleFileName(nullptr, path, MAX_PATH);
	//取不到路径，或路径太长放不下 "setting.ini"（11 个字符 + 结尾的 '\0'）时退回相对路径，
	//否则下面的 _tcscat_s 会越界并触发无效参数处理器，直接终止进程
	if (len == 0 || len + 12 > MAX_PATH) {
		_tcscpy_s(path, _T("setting.ini"));
		return path;
	}

	TCHAR* slash = _tcsrchr(path, _T('\\'));
	if (!slash) {
		_tcscpy_s(path, _T("setting.ini"));
		return path;
	}

	*(slash + 1) = _T('\0');
	_tcscat_s(path, _T("setting.ini"));
	return path;
}
#define SETTING_PATH  GetSettingPath()