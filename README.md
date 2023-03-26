# 介绍

本模块用于在内核态监控 CPU 使用率。在实现上主要就是对 htop 和 `/proc/stat` 的实现的简单缝合，配合内核态时钟来定时获取数据，最后配合 procfs 来实现和用户态的交互，用于配置 CPU 监控阈值。

环境：opencloudos 8.6，内核版本 5.4.119

# 实现

## 数据结构

CPU 状态数据的定义从 htop 的源代码中获取，`CPUData` 来自 `linux/LinuxProcessList.h`，可以将其中的 `unsigned long long int` 替换成内核模块常用的 `u64`。

`cpus` 全局变量是一个 `CPUData` 数组，将在内核模块初始化的时候根据 CPU 核心数使用 `vmalloc` 初始化数组内存。定义在 `stat.c` 中，在 `cpu.c` 中可以以外部变量形式引入。

## 获取数据 `get_stat()`

根据 htop 的 `LinuxProcessList_scanCPUTime()` 函数，htop 是通过读取 `/proc/stat` 来读取相关参数的，在内核中读文件不太合适，不如直接把 `/proc/stat` 的实现抄过来（把 Linux 内核源码的 `fs/proc/stat.c` 抄到了模块的 `stat.c` 文件中），替换掉 htop 中读文件 -> 解析文件内容的部分，正好也不需要再解析了。

其中模块中的 `get_stat()` 对应原文件中的 `show_stat()`，并做了修改：

- 裁剪掉与 `intr`、`ctxt` 等相关的计算、显示部分的代码
- 将 procfs 的输出函数改成写入 `cpus` 数组的函数 `write_into_cpus()`

同时由于函数内部需要，还应该把 `get_idle_time` 和 `get_iowait_time` 一起抄进来。

注意引入头文件 `linux/kernel_stat.h`。

## 存储数据 `write_into_cpus()`

这个函数的主要作用是计算获取得到的数据并存入 `cpuData` 数组，

主要代码来源是 htop 的 `LinuxProcessList_scanCPUTime()` 函数，去除前面的 `/proc/stat` 读取与解析的代码，直接传入 `get_stat()` 函数获取到的数据，修改写入数据的目的地为 `cpuData` 数组。

注意还需引入 `saturatingSub()` 辅助函数。

## 计算占用率 `Platform_setCPUValues()`

根据 [编程获取Linux的内存占用和CPU使用率](https://whoisnian.com/2020/01/30/%E7%BC%96%E7%A8%8B%E8%8E%B7%E5%8F%96Linux%E7%9A%84%E5%86%85%E5%AD%98%E5%8D%A0%E7%94%A8%E5%92%8CCPU%E4%BD%BF%E7%94%A8%E7%8E%87/)（[存档](https://web.archive.org/web/20220808010835/https://whoisnian.com/2020/01/30/%E7%BC%96%E7%A8%8B%E8%8E%B7%E5%8F%96Linux%E7%9A%84%E5%86%85%E5%AD%98%E5%8D%A0%E7%94%A8%E5%92%8CCPU%E4%BD%BF%E7%94%A8%E7%8E%87/)） 这一文章的介绍，可以在 htop 的代码中很容易找到 `linux/Platform.c` 文件中的 `Platform_setCPUValues()` 函数。这是用于 Linux 系统计算各 CPU 占用率的算法，抄到了模块的 `cpu.c` 文件中了，只要抄计算部分的即可，数据来源从全局变量 `cpus` 中读取。`detailedCPUTime` 分支如果不需要考虑虚拟化和并不准确的 IOWAIT 的话，可以考虑只抄 `else` 分支的内容。

需要注意的是，内核态用不到浮点数计算，因此也没有 `double` 数据类型，不要在代码中出现浮点数，并且计算的时候需要改成先乘以 100 再除以 `total`，避免先除再乘导致数据消失了。

此外还有来自 htop 的一些宏定义需要抄到 `cpu.h` 中并引入到 `.c` 文件中。`MAXIMUM` 在 htop 源码中没有，可能是来源于标准库，自己手写一个。`CLAMP` 中有用到 `assert` 功能，在内核态没有，可以删掉相关的判断语句，直接实现功能（当然还是需要保证条件符合原 `assert` 中的条件）。

## 内核定时器回调函数 `timer_task()`

定时器需要定期通过 `get_stat()` 获取 CPU 状态数据并使用 `Platform_setCPUValues()` 计算 CPU 占用率，计算的结果将输出到内核信息，如果不需要打印正常值可以注释掉 `timer_task()` 中的相关分支。

随后与预设的阈值比较，检查是否超出阈值，如果超出，输出到内核信息。

在结束之后需要重设定时器的计时。

输出效果：

```shell
$ dmesg
[3303.371034] CPU 0: 72
[3303.371076] WARNING: CPU 1: 69，larger than alert threshold 66!
[3303.371110] CPU 2: 21
[3303.371127] CPU 3: 30
[3303.371144] WARNING: avarage CPU usage: 49，larger than alert threshold 40!
```

## procfs 接口编写

procfs 主要就是用于与内核态交互，配置和显示 CPU 使用率监控的阈值，目标文件为 `/proc/cpu_threshold`。

在读取文件时，读取 `alert_threshold[]` 数组并使用 `sprintf` 格式化输出。在写入文件时，使用 `sscanf` 做格式化输入，并在判断数据合规后写入 `alert_threshold[]` 数组。

注意字符串输入输出的缓冲区要足够大，能放下所有内容，不然会导致缓冲区溢出与内核崩溃，最好是能提前检查缓冲区是否够用。

以下是使用示例，假设有四个逻辑 CPU，则 CPU 0-3 是每个单独的 CPU 占用率，而 CPU 4 则是平均 CPU 占用率：

```shell
$ echo "1 66" >> /proc/cpu_threshold 
$ echo "4 40" >> /proc/cpu_threshold
$ cat /proc/cpu_threshold 
CPU 0 threshold: 0 %.
CPU 1 threshold: 66 %.
CPU 2 threshold: 0 %.
CPU 3 threshold: 0 %.
CPU 4 (avaerage usage) threshold: 40 %.
```

## 模块加载 `cpu_monitor_init()`

根据在线 CPU 数分配 `cpus`、`alert_threshold` 数组的内存空间并清空，随后使用 `get_stat()` 获取初次 CPU 数据，初始化并添加定时器，初始化 procfs 相关内容。

## 模块卸载 `cpu_monitor_exit()`

模块卸载时需要释放 `cpus`、`alert_threshold` 数组，删除定时器 `timer`，删除 procfs 下的文件。

# Brief introduction in English

This is a simple kernel module implemented for CPU monitoring.

The code is mainly based on the implementation of htop and `/proc/stat`.

You just need to `make` it and `sudo insmod cpus.ko`, then use `/proc/cpu_threshold` to set the alert threshold.

If normal values do not need to be printed, the relevant branches in `timer_task()` can be commented out.

Assuming there are four logical CPUs, CPU 0-3 are the individual CPU usage percentages, and CPU 4 is the average CPU usage percentage:

```shell
$ echo "1 66" >> /proc/cpu_threshold 
$ echo "4 40" >> /proc/cpu_threshold
$ cat /proc/cpu_threshold 
CPU 0 threshold: 0 %.
CPU 1 threshold: 66 %.
CPU 2 threshold: 0 %.
CPU 3 threshold: 0 %.
CPU 4 (avaerage usage) threshold: 40 %.
```

You can get alert messages in kernel message (use `dmesg`).

# 参考资料

1. [Linux内核由4.4升级到5.4做的一些改变（一）——定时器timer_timer_setup 回调函数_向雨而虹的博客-CSDN博客](https://blog.csdn.net/weixin_53883224/article/details/124415988)（[存档](https://web.archive.org/web/20220608012434/https://blog.csdn.net/weixin_53883224/article/details/124415988)）
2. [proc(5) - Linux manual page](https://man7.org/linux/man-pages/man5/proc.5.html)
3. [htop-dev/htop: htop - an interactive process viewer](https://github.com/htop-dev/htop)
4. [编程获取Linux的内存占用和CPU使用率](https://whoisnian.com/2020/01/30/%E7%BC%96%E7%A8%8B%E8%8E%B7%E5%8F%96Linux%E7%9A%84%E5%86%85%E5%AD%98%E5%8D%A0%E7%94%A8%E5%92%8CCPU%E4%BD%BF%E7%94%A8%E7%8E%87/)（[存档](https://web.archive.org/web/20220808010835/https://whoisnian.com/2020/01/30/%E7%BC%96%E7%A8%8B%E8%8E%B7%E5%8F%96Linux%E7%9A%84%E5%86%85%E5%AD%98%E5%8D%A0%E7%94%A8%E5%92%8CCPU%E4%BD%BF%E7%94%A8%E7%8E%87/)）
