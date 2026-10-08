
# ScrollSound 简介
[![Badge](https://img.shields.io/badge/link-996.icu-%23FF4D5B.svg?style=flat-square)](https://996.icu/#/en_US)
[![LICENSE](https://img.shields.io/badge/license-Anti%20996-blue.svg?style=flat-square)](https://github.com/996icu/996.ICU/blob/master/LICENSE)
[![GitHub release](https://img.shields.io/github/release/SWDaby/ScrollSound.svg?style=flat-square)](https://github.com/SWDaby/ScrollSound/releases/latest)
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/swdaby/scrollsound/total?style=flat-square&labelColor=%23636363&color=%2325c2a0)<br>

**ScrollSound**是一个可以让你在任务栏空白处用滚轮控制音量的小工具。一个看似鸡肋用了却离不开的小功能。<br>
目前支持win10、win11。<br>



## ✨相关链接：
**下载：**<br>
[Github](https://github.com/SWDaby/ScrollSound/releases/latest)<br>
[Gitee](https://gitee.com/swdaby/ScrollSound/releases/)<br>
[阿里云盘](https://www.aliyundrive.com/s/TBGXFokBRB3) 提取码：`bg1m`<br>

**依赖环境：**<br>
[最新支持的 Visual C++ 可再发行程序包下载 | Microsoft Docs](https://docs.microsoft.com/zh-CN/cpp/windows/latest-supported-vc-redist?view=msvc-170)<br>
如果程序启动时提示找不到dll文件，请下载并安装Microsoft Visual C++ 运行环境。<br>


本工具使用了TrafficMonitor项目里的一些类，点击围观[大佬](https://github.com/zhongyang219/TrafficMonitor)<br>

本项目[图标资源](https://www.flaticon.com/)<br>

## ✨使用

双击打开，将鼠标移至任务栏空白处：

- 滚动滚轮：调节音量大小
- 按下滚轮（中键）：静音 / 取消静音
- 双击左键：打开 `setting.ini` 里配置的程序，默认是任务管理器

为什么要用？为了优雅。

## ⚙️双击动作配置

程序会在 exe 同目录下生成 `setting.ini`（UTF-16 LE 编码），双击动作由它决定：

```ini
[setting]
Administrator=0
DoubleClickEnabled=1
DoubleClickCommand=taskmgr.exe
DoubleClickArgs=
DoubleClickInterval=300
DoubleClickRect=0
```

| 配置项 | 说明 |
| --- | --- |
| `DoubleClickEnabled` | `1` 启用双击动作，`0` 关闭 |
| `DoubleClickCommand` | 要打开的程序、文件或协议，支持中文路径；**留空**则不执行任何动作 |
| `DoubleClickArgs` | 传给该程序的命令行参数，可留空 |
| `DoubleClickInterval` | 双击判定时间（毫秒）：两次左键按下的间隔不超过它才算双击。默认 `300`（比系统的 500ms 更严格，判定不至于太宽泛）；配 `0` 跟随系统双击时间。可填 50~2000 |
| `DoubleClickRect` | 双击判定的位置容差（像素，横向/纵向各自的最大偏移）。配 `0`（默认）跟随系统双击矩形；判定太宽泛时可填 1~50 收紧 |

常用写法：

| 想双击打开 | `DoubleClickCommand=` |
| --- | --- |
| 任务管理器 | `taskmgr.exe` |
| 资源监视器 | `resmon.exe` |
| 声音设置（音量合成器） | `ms-settings:appsvolume` |
| 截图工具 | `ms-screenclip:` |
| 任意程序 | `D:\工具\Everything.exe`（中文路径也可以） |

**配置是热更新的**：程序每秒检查一次 `setting.ini`，改完保存后 1 秒内自动生效，不用重启，也不用重新 HOOK。

托盘菜单里的 **双击判定时间** 子菜单还能直接切换常用档位（150 / 200 / 250 / 300 / 400 / 500 毫秒或跟随系统设置），选中后会写回 `setting.ini` 并立即生效；菜单标题里显示的就是当前实际生效的毫秒数。若在 `setting.ini` 里手填了非档位的值（如 `350`），菜单不会勾选任何档位，但标题仍会显示实际值。

注意事项：

- `setting.ini` 请保持 **UTF-16 LE** 编码。用记事本直接编辑保存即可（记事本会沿用原编码）；若被另存为「UTF-8 带 BOM」，程序下次启动会自动转回 UTF-16 LE；但「UTF-8 无 BOM」无法自动识别，此时中文路径可能乱码。
- 双击动作只对**任务栏空白处**生效，双击任务栏上的应用图标、托盘图标不会有任何动作。
- 程序启动子进程时的工作目录是 ScrollSound.exe 所在目录，所以配置里也可以写相对路径（如 `.\tool.exe`）。
- 配置的程序打不开时（路径写错等），程序会退回打开任务管理器，不会静默失效。

## 截图

托盘菜单：

<div align=center><img src="./imgs/snipaste.png" ></div>

![](./imgs/Animation.gif)

## 已知问题：

🔴Win11多屏不能使用。(未解决❌)<br>
🔴不能降权，需重启应用才能降权。(不影响使用，未解决❌)<br>

<div align=center><img src="./imgs/Reward.png"></div>

