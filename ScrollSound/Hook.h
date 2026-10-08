#pragma once
#include "framework.h"
#include "WinVersionHelper.h"
//void SetKeyboardHook(DWORD threadId);
void SetMouseHook(DWORD threadId);
void MoveMouseHook();
void ReHook();

//双击任务栏空白处：钩子里只负责判定并投递该消息，真正的动作在主消息循环里执行
#define WM_DBLCLICK_TASKBAR (WM_APP + 1)
void EnsureSettingFileUnicode();		//wWinMain 一开始调用，保证 setting.ini 是 UTF-16LE
void RunTaskbarDoubleClickAction();		//在 WndProc 里响应 WM_DBLCLICK_TASKBAR

//双击判定参数的热更新接口：改 setting.ini 后不用重启，也不用重新 hook
void ReloadDoubleClickSetting();			//立即重读 setting.ini 里的双击配置
void StartDoubleClickConfigWatch();			//启动配置监视定时器（程序启动时调用一次）
int  GetDoubleClickIntervalSetting();		//返回 ini 里配的判定时间（毫秒），0 表示跟随系统
void SetDoubleClickIntervalSetting(int ms);	//写入 ini 并立即生效（0 表示跟随系统）