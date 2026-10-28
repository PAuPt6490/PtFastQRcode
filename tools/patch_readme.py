# -*- coding: utf-8 -*-
"""Refresh README numbers and the implementation notes after the size/memory pass."""
import io

path = r"C:\PtFastQRcode\README.md"
text = io.open(path, encoding="utf-8").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


sub("""纯 Win32 C++ 编写,静态链接,**单文件 EXE(约 290 KB)**,无需安装任何运行库。""",
    """纯 Win32 C++ 编写,**单文件 EXE(约 126 KB)**,不链接 C 运行库、不依赖 GDI+,
只调用系统 DLL,无需安装任何运行库。""", "intro")

sub("""产物:`dist\\PtFastQRcode.exe`(静态 CRT `/MT` + LTCG,单文件)。""",
    """产物:`dist\\PtFastQRcode.exe`(单文件,约 126 KB)。

### 体积与内存是怎么压下来的

| 措施 | 效果 |
| --- | --- |
| 图标只保留 16/20/24/32/48 + 256(调色板 PNG) | 64 KB → 27 KB |
| `/NODEFAULTLIB` + 自写入口(`crt_stub.cpp`/`crt_mem.c`) | 去掉静态 CRT,**−106 KB** |
| 去掉 GDI+ 依赖,圆角改 GDI `RoundRect`,PNG 改 WIC 编码 | 少链接一个组件,`gdiplus.dll` 不再加载 |
| 字体改为首次开窗时创建;`comdlg32.dll` 首次保存时才加载 | 托盘驻留态少 4 个字体、少一个 DLL |
| 主窗口按脏区绘制(状态文字变化不再重画二维码) | 局部刷新成本≈0 |

结果(本机 150% DPI,Windows 11):

| 指标 | 优化前 | 优化后 |
| --- | --- | --- |
| EXE 体积 | 291.5 KB | **126 KB** |
| 私用内存(仅托盘) | 1.87 MB | **1.54 MB** |
| 私用内存(窗口打开) | 2.55 MB | **2.1 MB** |
| 工作集(窗口打开) | 20 MB | **15.8 MB** |
| 整窗重绘 | 约 25 ms | **约 1.4–6.5 ms** |
| 启动到窗口可见 | — | 约 190 ms(5 次平均) |

> 说明:去掉 GDI+ 后,按钮/卡片的圆角由 GDI 绘制,边缘不再抗锯齿(1 px 阶梯);
> 窗口本身的圆角本来就由区域裁剪实现,观感一致。""", "build notes")

sub("""│  ├─ app/qrimage.cpp        二维码位图渲染、PNG 保存、剪贴板""",
    """│  ├─ app/qrimage.cpp        二维码 DIB 渲染、WIC 编码 PNG、剪贴板
│  ├─ app/text_util.cpp      取代 CRT 的字符串/路径工具
│  ├─ app/crt_stub.cpp       自写入口、堆分配、STL 终止钩子(无 CRT)
│  ├─ app/crt_mem.c          memcpy/memset/宽字符原语(无 CRT)""", "tree")

sub("""| 资源占用 | 私有内存:空闲 1.9 MB,打开窗口约 2.6 MB(反复开关不增长);调用系统保存对话框后系统会加载资源管理器扩展,私有内存升至约 15 MB(Windows 通用对话框行为,非本程序泄漏) |""",
    """| 资源占用 | 私有内存:仅托盘 1.54 MB,打开窗口约 2.1 MB(反复开关不增长);调用系统保存对话框后系统会加载资源管理器扩展,私有内存升至约 13 MB(Windows 通用对话框行为,非本程序泄漏) |
| 无运行库依赖 | 导入表仅 kernel32/user32/gdi32/shell32/ole32/comctl32/advapi32/uxtheme,无 CRT、无 GDI+ |""",
    "verification")

io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("README updated")
