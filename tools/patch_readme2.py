# -*- coding: utf-8 -*-
"""README: document the child-process dialog and the new memory numbers."""
import io

path = r"C:\PtFastQRcode\README.md"
text = io.open(path, encoding="utf-8").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


sub("""纯 Win32 C++ 编写,**单文件 EXE(约 126 KB)**,不链接 C 运行库、不依赖 GDI+,
只调用系统 DLL,无需安装任何运行库。""",
    """纯 Win32 C++ 编写,**单文件 EXE(约 128 KB)**,不链接 C 运行库、不依赖 GDI+,
只调用系统 DLL,无需安装任何运行库;窗口关闭即销毁,内存回到托盘基线。""", "intro")

sub("""| 主窗口按脏区绘制(状态文字变化不再重画二维码) | 局部刷新成本≈0 |""",
    """| 主窗口按脏区绘制(状态文字变化不再重画二维码) | 局部刷新成本≈0 |
| 窗口隐藏即销毁(不再只是 `SW_HIDE`) | 关窗后回到托盘基线 |
| 保存对话框放到**子进程**里开 | Shell 扩展不会常驻,省下约 9–13 MB |
| COM 延迟到真正需要时才初始化 | 托盘态不初始化 ole32 |""", "table")

sub("""| 私用内存(仅托盘) | 1.87 MB | **1.54 MB** |
| 私用内存(窗口打开) | 2.55 MB | **2.1 MB** |
| 工作集(窗口打开) | 20 MB | **15.8 MB** |""",
    """| 私用内存(仅托盘) | 1.87 MB | **1.48 MB** |
| 私用内存(窗口打开) | 2.55 MB | **2.24 MB** |
| 工作集(窗口打开) | 20 MB | **17.8 MB** |
| 保存后回落(工作集) | 65 MB(不回落) | **1.6 MB** |""", "numbers")

sub("""| EXE 体积 | 291.5 KB | **126 KB** |""",
    """| EXE 体积 | 291.5 KB | **128 KB** |""", "size")

sub("""> 说明:去掉 GDI+ 后,按钮/卡片的圆角由 GDI 绘制,边缘不再抗锯齿(1 px 阶梯);
> 窗口本身的圆角本来就由区域裁剪实现,观感一致。""",
    """> 说明:去掉 GDI+ 后,按钮/卡片的圆角由 GDI 绘制,边缘不再抗锯齿(1 px 阶梯);
> 窗口本身的圆角本来就由区域裁剪实现,观感一致。

### 保存对话框为什么另起进程

`GetSaveFileNameW` 会把资源管理器的 Shell 扩展加载进调用它的进程,COM 只要还持有引用
就不会卸载:实测对话框打开时私有内存 46 MB,关掉后仍停在 15 MB。放进子进程后,主进程
全程不加载 `comdlg32`,对话框关掉时那部分内存随子进程一起消失,主进程回落到 1.6 MB。

子进程就是本程序的 `--pick-save` 模式:弹出同样的系统对话框,把选中的路径写回临时文件后退出;
主进程用 `MsgWaitForMultipleObjects` 等待并继续处理消息,界面不会卡住。""", "dialog note")

io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("README updated")
