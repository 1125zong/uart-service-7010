# UART Service for Linux 完整工程说明

## 目录
1. [项目概述](#1-项目概述)
2. [系统架构](#2-系统架构)
3. [硬件抽象层 (HAL)](#3-硬件抽象层-hal)
4. [应用层 (APP)](#4-应用层-app)
5. [系统抽象层 (SAL)](#5-系统抽象层-sal)
6. [用户界面模块 (Screen)](#6-用户界面模块-screen)
7. [通信协议](#7-通信协议)
8. [构建与部署](#8-构建与部署)
9. [开发指南](#9-开发指南)

---

## 1. 项目概述

### 1.1 项目背景
本项目是一个运行在 Linux 平台上的嵌入式**UART-to-Network 服务程序**,旨在实现串行通信 (UART) 与网络通信之间的桥接。适用于工业控制、设备远程管理等场景。

### 1.2 目标用户
- 嵌入式系统开发者
- 工业设备制造商
- 物联网网关开发人员

### 1.3 核心功能
- **UART 通信管理**:通过 HAL 层操作串口设备，收发数据
- **网络协议支持**:实现基于 UDP 的设备发现、TCP/UDP 数据透传
- **设备状态报告**:提供设备基本信息、串口配置、网络状态等查询接口
- **固件更新**:支持远程固件升级流程
- **定时调度**:使用定时器触发周期性任务 (如心跳、监控)
- **日志与环形缓冲区**:记录运行日志，保障数据不丢失
- **屏幕 UI 模块 (可选)**:本地 LCD 界面显示与交互 (含菜单、设置项)

### 1.4 技术栈
- **编程语言**: C99
- **操作系统**: Linux (ARM 架构)
- **编译器**: arm-linux-gnueabihf-gcc
- **线程库**: POSIX pthreads
- **网络库**: 原生 socket 编程
- **依赖库**: `-lpthread`, `-lm`, `-lrt`

### 1.5 关键特性
- 跨平台 HAL 抽象层支持硬件移植
- 多线程架构处理串口、网络、定时任务
- 支持设备配置、状态上报、网络搜索、固件升级等完整功能
- 可调试模式构建，便于开发与维护
- 模块化设计，各功能独立编译

---

## 2. 系统架构

### 2.1 分层架构图

```
+----------------------------------------------------------+
|                    Application Layer                      |
|  (设备管理、网络配置、协议处理、实时调度、UI 界面)          |
+----------------------------------------------------------+
|              Hardware Abstraction Layer (HAL)             |
|        (UART、Flash、Timer、RingBuffer、Log)              |
+----------------------------------------------------------+
|            System Abstraction Layer (SAL)                 |
|     (Task、Semaphore、Message Queue - POSIX 封装)           |
+----------------------------------------------------------+
|                   Linux Kernel                            |
|         (UART Driver、pthread、rt、socket)                |
+----------------------------------------------------------+
```

### 2.2 核心模块划分

#### 2.2.1 主程序入口
- **文件**: `init_app_nport.c`
- **职责**: 程序入口，打印编译信息，调用 `app_start()`

#### 2.2.2 应用初始化模块
- **文件**: `APP/app_init.c`
- **职责**: 
  - 初始化全局资源
  - 加载设备配置
  - 初始化通道状态
  - 测试 UART
  - 创建应用任务

#### 2.2.3 任务创建模块
- **文件**: `APP/app_task_creation.c`
- **职责**: 创建所有系统线程
  - 配置任务管理器 (优先级 60)
  - 实时调度器 (优先级 50)
  - 连接管理器 (优先级 70)
  - UDP 搜索任务 (优先级 75)
  - 报告管理任务 (优先级 80)
  - 按键处理任务 (优先级 90)

### 2.3 线程模型

系统采用多线程并发架构，各线程职责清晰:

| 线程名称 | 优先级 | 栈大小 | 职责描述 |
|---------|--------|--------|----------|
| RealTimeScheduler | 50 | 32KB | 实时数据处理，UART 与网络数据转发 |
| ConfigTaskManager | 60 | 32KB | 配置管理，网络模式切换 |
| ConnectionManager | 70 | 8KB | 网络连接管理，accept/connect,分发 fd |
| UdpSearch | 75 | 32KB | UDP 设备发现与应答 |
| ReportManager | 80 | 32KB | TCP 端口 5000 监听，处理设备配置命令 |
| KeyProcess | 90 | 32KB | 按键处理，UI 交互 |

---

## 3. 硬件抽象层 (HAL)

### 3.1 概述
HAL 层将底层硬件操作抽象化，便于在不同平台间移植。所有硬件相关操作都通过 HAL 层提供统一接口。

### 3.2 核心模块

#### 3.2.1 UART 硬件抽象 (`hal_axi16550.c/h`)
- **功能**: AXI16550 UART 控制器驱动
- **基地址**: `0x41200000` (PL_AXI_UART_BASE)
- **寄存器映射**:
  - RBR/THR: `0x1000` (接收/发送保持寄存器)
  - IER: `0x1004` (中断使能寄存器)
  - IIR/FCR: `0x1008` (中断识别/ FIFO 控制)
  - LCR: `0x100C` (线控制寄存器)
  - MCR: `0x1010` (MODEM 控制寄存器)
  - LSR: `0x1014` (线状态寄存器)
- **API**:
  - `axi165502CInit()`: 初始化 UART
  - `sysAxiWriteLong()/sysAxiReadLong()`: AXI 总线读写

#### 3.2.2 Flash 存储抽象 (`hal_flash.c/h`)
- **功能**: Flash 存储器读写擦除操作
- **地址映射**:
  - 基本信息：`0xE9F00000` (64KB)
  - 网络信息：`0xE9F10000` (64KB)
  - 恢复标志：`0xE9F20000` (64KB)
  - 总大小：1MB
- **Linux 实现**: 通过文件模拟 Flash 操作，路径 `config_data/`
- **API**:
  - `hal_flash_write()`: 写入 Flash
  - `hal_flash_read()`: 读取 Flash
  - `hal_flash_erase()`: 擦除扇区
  - `hal_flash_cleanup()`: 释放资源

#### 3.2.3 环形缓冲区 (`hal_ringbuffer.c/h`)
- **功能**: 高效的 FIFO 数据结构，用于 UART 和网络数据缓冲
- **特点**: 
  - 支持单字节和数组操作
  - 无锁设计 (需外部同步)
  - 容量为 2 的幂次减 1
- **API**:
  - `ring_buffer_init()`: 初始化
  - `ring_buffer_queue()`: 入队
  - `ring_buffer_dequeue()`: 出队
  - `ring_buffer_is_empty/full()`: 状态检查

#### 3.2.4 定时器 (`hal_timer.c/h`)
- **功能**: 高精度定时任务调度
- **精度**: 100 微秒 (TIMER_TICK_US)
- **API**:
  - `app_start_hz()`: 启动 Hz 级定时器
  - `app_register_task()`: 注册定时任务

#### 3.2.5 日志系统 (`hal_log.c/h`)
- **功能**: 分级日志输出
- **级别**: DEBUG, INFO, WARN, ERROR
- **宏控制**: 通过 DEBUG 宏编译时控制

#### 3.2.6 通用 HAL (`hal_com.c/h`)
- **功能**: 通用类型定义和常量
- **定义**:
  - `STATUS`: int32_t
  - `OK`: 0, `ERROR`: -1
  - `UART_HW_FIFO_SIZE`: 256

---

## 4. 应用层 (APP)

### 4.1 设备管理模块

#### 4.1.1 设备配置 (`app_dev.c/h`, `app_dev_cfg.h`)
- **核心结构**: `SystemConfiguration`
  - `DeviceSettings`: 设备全局配置
  - `ChannelState[NUM_PORTS]`: 16 个通道状态
- **配置项**:
  - 设备型号、序列号、固件版本
  - 网络设置 (IP、MAC、网关)
  - 串口参数 (波特率、数据位、停止位、校验)
  - 工作模式 (RealCOM、TCP Server/Client、UDP)
  - 数据打包配置
- **API**:
  - `dev_config_init()`: 初始化配置
  - `dev_config_load_defaults()`: 加载默认配置
  - `dev_config_save()`: 保存配置到 Flash
  - `dev_network_settings_apply()`: 应用网络设置

#### 4.1.2 资源配置 (`app_resource_init.c`)
- **职责**: 初始化全局资源
  - 创建消息队列
  - 创建互斥量
  - 初始化全局变量

### 4.2 网络通信模块

#### 4.2.1 网络连接管理 (`app_net_con.c/h`)
- **核心任务**: `ConnectionManagerTask`
- **职责**:
  - 监听所有 TCP Server 端口
  - 接受新客户端连接 (accept)
  - 管理 TCP Client 非阻塞连接 (connect)
  - 实施最大连接数限制
  - 通过消息队列分发 socket fd 给业务任务
- **设计原则**:
  - 单一职责：只负责连接建立、分发与计数
  - 事件驱动：使用 select() 模型
  - 配置驱动：由 g_system_config 驱动

#### 4.2.2 网络调度 (`app_net_scheduler.c/h`)
- **核心任务**: `NetworkSchedulerTask`
- **职责**:
  - 检查新连接消息队列
  - 处理网络数据接收 (recv)
  - 处理网络数据发送 (send)
  - 清理断开的数据连接

#### 4.2.3 网络配置 (`app_net_cfg.c/h`)
- **职责**: 网络参数配置与管理
  - IP 地址、子网掩码、网关配置
  - MAC 地址管理
  - 网口速度检测

#### 4.2.4 网络协议 (`app_net_proto.c/h`)
- **职责**: 网络协议处理
  - 数据帧解析
  - 协议转换

### 4.3 UART 通信模块

#### 4.3.1 UART 数据处理 (`app_uart.c/h`)
- **职责**:
  - UART 参数配置 (波特率、数据位等)
  - UART 数据读取
  - 数据打包 (Delimiter 处理)
  - 计算网络发送参数
- **波特率表**: 支持 50-921600bps
- **API**:
  - `init_usart()`: 初始化串口
  - `calculate_send_parameters()`: 计算发送参数

#### 4.3.2 实时数据处理 (`app_realtime.c/h`)
- **核心任务**: `RealTimeSchedulerTask`
- **职责**:
  - 周期性轮询 UART 状态
  - 从 UART 读取数据存入环形缓冲区
  - 从网络接收数据写入 UART
  - 执行数据打包逻辑
  - 统计流量

### 4.4 设备服务模块

#### 4.4.1 UDP 搜索 (`app_udp_search.c/h`)
- **核心任务**: `UdpSearchTask`
- **监听端口**: 48899
- **搜索消息**: "SEARCH_DEVICE_WQ"
- **响应内容**:
  - 设备型号、序列号、名称
  - 固件版本、设备状态
  - 双网口 IP/MAC/速度信息
  - LCM 在位状态、电源状态

#### 4.4.2 报告管理 (`APP/report/*.c/h`)
- **核心任务**: `ReportManagerTask`
- **监听端口**: TCP 5000
- **协议格式**: `[A5 A5][FuncID][SubID][Data][5A 5A]`
- **功能分类**:
  - **0x01**: 设备概览
  - **0x02**: 设备名称操作
  - **0x03**: 网络设置
  - **0x04**: 串口设置
  - **0x05**: 串口模式
  - **0x06**: 读取工作模式
  - **0x07**: 设备状态监控
  - **0x08**: 设备管理
- **客户端管理**: 最多支持 MAX_REPORT_CLIENTS 个客户端

#### 4.4.3 固件更新 (`app_update.c/h`)
- **服务端口**: TCP 19001
- **固件包格式**: `fw_package_header_t`
  - Magic Number 校验
  - CRC 校验
  - 版本信息
  - BIT 流、OS、APP 数据段
- **升级流程**:
  1. 接收完整固件包
  2. 执行 5 项校验
  3. 发送 STATUS_OK_TO_PROCEED
  4. 写入 Flash
  5. 发送 STATUS_WRITE_COMPLETE
  6. 更新环境变量 (app_env)
- **安全机制**:
  - 双向应答协议
  - 不依赖文件系统
  - 冗余环境变量存储

### 4.5 配置持久化模块

#### 4.5.1 更新环境 (`app_update_env.c/h`)
- **职责**: 管理 Flash 中的环境变量
- **API**:
  - `app_fw_setenv()`: 设置环境变量
  - `app_fw_getenv()`: 获取环境变量
  - `app_fw_save()`: 保存到 Flash

---

## 5. 系统抽象层 (SAL)

### 5.1 概述
SAL 层将 VxWorks API 映射到 Linux POSIX API，实现跨平台兼容。

### 5.2 核心抽象

#### 5.2.1 任务抽象
- **VxWorks**: `taskSpawn()`, `taskDelay()`
- **Linux**: `pthread_create()`, `sleep()`
- **封装函数**:
  - `task_spawn_wrapper()`: 创建线程
  - `task_delay_wrapper()`: 延时
  - `task_suspend_wrapper()`: 挂起线程
  - `task_resume_wrapper()`: 恢复线程

#### 5.2.2 信号量抽象
- **VxWorks**: `semMCreate()`, `semTake()`, `semGive()`
- **Linux**: `sem_init()`, `sem_wait()`, `sem_post()`
- **封装函数**:
  - `sem_m_create()`: 创建互斥量
  - `sem_take()`: 等待信号量
  - `sem_give()`: 释放信号量
  - `sem_delete()`: 删除信号量

#### 5.2.3 消息队列抽象
- **VxWorks**: `msgQCreate()`, `msgQSend()`, `msgQReceive()`
- **Linux**: 自定义链表实现
- **封装函数**:
  - `msg_q_create()`: 创建消息队列
  - `msg_q_send()`: 发送消息
  - `msg_q_receive()`: 接收消息
  - `msg_q_delete()`: 删除消息队列

### 5.3 数据类型映射

| VxWorks 类型 | Linux 类型 |
|-------------|-----------|
| TASK_ID | pthread_t |
| SEM_ID | sem_t* |
| MSG_Q_ID | struct msg_queue* |
| STATUS | int32_t |

---

## 6. 用户界面模块 (Screen)

### 6.1 概述
可选的 LCD 显示模块，提供本地人机交互界面。

### 6.2 核心组件

#### 6.2.1 屏幕驱动 (`my_screen_driver.c/h`)
- **分辨率**: 根据硬件配置
- **字符宽度**: FONT_WIDTH
- **API**:
  - `clear()`: 清屏
  - `draw_text()`: 绘制文本
  - `refresh()`: 刷新显示

#### 6.2.2 按键处理 (`key.c/h`)
- **核心任务**: `KeyProcessTask`
- **职责**: 扫描按键，生成键值事件
- **事件类型**: KEY_EVENT_NONE, KEY_PRESS, KEY_RELEASE

#### 6.2.3 UI 状态机 (`ui_state_*.c/h`)
采用状态机模式管理 UI 界面:

- **主界面** (`ui_state_main.c`):
  - 显示设备型号、IP 地址、状态
  - 任意键进入主菜单

- **主菜单列表** (`ui_state_menu_list.c`):
  - 设备信息
  - 网络设置
  - 串口设置
  - 系统设置

- **设备信息** (`ui_state_device_info.c`):
  - 显示型号、序列号、固件版本

- **网络设置** (`ui_state_network.c`):
  - IP 地址配置
  - 子网掩码配置
  - 网关配置

- **锁定状态** (`ui_state_lock.c`):
  - 设备锁定/解锁
  - 密码输入

- **恢复出厂** (`ui_state_factory_reset.c`):
  - 确认对话框
  - 执行重置

- **重启** (`ui_state_restart.c`):
  - 重启确认

### 6.3 状态机框架

```c
typedef struct {
    void (*on_enter)(ui_manager_t* manager);
    void (*handle_input)(ui_manager_t* manager, key_event_t event);
    void (*on_exit)(ui_manager_t* manager);
} ui_state_t;
```

---

## 7. 通信协议

### 7.1 设备发现协议 (UDP)

#### 7.1.1 搜索请求
- **端口**: UDP 48899
- **消息**: "SEARCH_DEVICE_WQ"

#### 7.1.2 响应格式 (256 字节)
```
偏移    长度    字段
0       40      型号名称
40      8       序列号
48      40      设备名称
88      4       固件版本
92      1       设备状态
93      1       网口数量
94      4       IP 地址 1
98      6       MAC 地址 1
104     1       LAN1 速度
105     4       IP 地址 2
109     6       MAC 地址 2
115     1       LAN2 速度
116     1       LCM 在位
117     1       电源 1 状态
118     1       电源 2 状态
119     4       接口类型数量
123     16      接口状态
139     1       设备锁定状态
140     16      接口锁定状态
156     256     端口工作状态 (16 端口×16 字节)
412     2       帧尾 (5A 5A)
```

### 7.2 设备配置协议 (TCP 5000)

#### 7.2.1 帧格式
```
帧头 (2B) + 功能 ID(1B) + 子 ID(1B) + 数据 (N) + 帧尾 (2B)
帧头：A5 A5
帧尾：5A 5A
```

#### 7.2.2 功能码定义

| 功能 ID | 子 ID | 功能描述 |
|--------|-------|----------|
| 0x01 | - | 设备概览 |
| 0x02 | 0x00 | 读取设备名 |
|      | 0x01 | 设置设备名 |
| 0x03 | 0x01 | 读取网络设置 |
|      | 0x02 | 设置网络设置 |
| 0x04 | 0x01 | 读取串口设置 |
|      | 0x02 | 设置单个串口 |
|      | 0x03 | 设置多个串口 |
| 0x05 | - | 串口模式设置 |
| 0x06 | - | 读取工作模式 |
| 0x07 | - | 设备状态监控 |
| 0x08 | - | 设备管理 |

#### 7.2.3 示例

**读取基础信息**
```
发送：A5 A5 01 01 5A 5A
接收：A5 A5 01 01 [256 字节数据] 5A 5A
```

**设置设备名**
```
发送：A5 A5 02 01 05 68 65 6C 6C 6F 5A 5A  (Hello)
返回：A5 A5 02 01 01 5A 5A  (成功)
```

**设置网络参数**
```
发送：A5 A5 03 02 01 02 c0 a8 05 05 ff ff ff 00 c0 a8 05 01 5A 5A
(IP:192.168.5.5, Mask:255.255.255.0, Gateway:192.168.5.1)
```

### 7.3 固件更新协议 (TCP 19001)

#### 7.3.1 固件包结构
```c
typedef struct {
    uint32_t magic;           // 魔术字校验
    uint32_t crc;             // CRC 校验
    uint8_t version[4];       // 版本号
    uint32_t bit_size;        // BIT 流大小
    uint32_t os_size;         // OS 大小
    uint32_t app_size;        // APP 大小
    // 后续为数据段...
} fw_package_header_t;
```

#### 7.3.2 升级流程
1. 客户端发送固件包
2. 服务端校验 (Magic、CRC、Version、Size)
3. 校验通过发送 `STATUS_OK_TO_PROCEED`
4. 写入 Flash
5. 发送 `STATUS_WRITE_COMPLETE`
6. 更新环境变量

#### 7.3.3 应答码
- `STATUS_OK_TO_PROCEED`: 0x01
- `STATUS_WRITE_COMPLETE`: 0x02
- `STATUS_ERROR`: 0xFF

---

## 8. 构建与部署

### 8.1 开发环境要求

#### 必需工具
- **编译器**: arm-linux-gnueabihf-gcc
- **构建工具**: GNU Make
- **Shell 环境**: Linux bash
- **Python**: 固件打包脚本

#### 可选工具
- **GDB**: 调试器 (配合-g 生成调试信息)
- **minicom/screen**: 串口调试
- **Wireshark**: 网络抓包分析

### 8.2 环境搭建

```bash
# 1. 安装 ARM 交叉编译工具链 (Ubuntu)
sudo apt install gcc-arm-linux-gnueabihf

# 2. 克隆项目
git clone <repository_url>
cd uart-service_-linux-master

# 3. 验证编译器
make check

# 4. 构建项目
make
```

### 8.3 构建命令

```bash
# 默认构建 debug 版本 (带-g 调试信息)
make

# 构建 release 版本 (-O2 优化)
make DEBUG=0

# 构建 UART 测试程序
make uart_test

# 清理构建产物
make clean

# 重新构建
make rebuild

# 显示构建信息
make info

# 检查构建环境
make check

# 运行应用程序 (需目标环境)
make run

# 安装到系统目录
make install  # 复制到/usr/local/bin
```

### 8.4 Makefile 配置

#### 编译器设置
```makefile
CC = arm-linux-gnueabihf-gcc
CFLAGS = -Wall -Wextra -std=c99 -D_REENTRANT -D_GNU_SOURCE -pthread
CFLAGS += -I./HAL -I./APP/inc -I./APP/screen -I./APP/report -I.

# Debug/Release配置
DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif
```

#### 源文件组织
- **HAL 源文件**: hal_com.c, hal_flash.c, hal_log.c, hal_ringbuffer.c, hal_timer.c, hal_axi16550.c, uart.c
- **APP 源文件**: app_dev.c, app_init.c, app_net_cfg.c, app_net_con.c, app_net_proto.c, app_net_scheduler.c, app_realtime.c, app_resource_init.c, app_task_creation.c, app_uart.c, app_udp_search.c, app_update.c, app_update_env.c
- **Report 源文件**: app_ports.c, app_report.c, app_report_basic.c, app_report_management.c, app_report_mode.c, app_report_network.c, app_report_serial.c, app_report_status.c
- **Screen 源文件**: key.c, my_screen_driver.c, ui_menu_system.c, ui_state_*.c
- **SAL 源文件**: system_abstraction_layer.c

#### 链接库
```makefile
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ -lpthread -lm -lrt
```

### 8.5 部署说明

#### 8.5.1 目标板部署
1. 编译生成 `uart_service` 可执行文件
2. 通过网络或串口传输到目标板
3. 赋予执行权限：`chmod +x uart_service`
4. 运行：`./uart_service`

#### 8.5.2 配置文件目录
在目标板创建 `config_data/` 目录:
```bash
mkdir -p config_data
```

#### 8.5.3 开机自启
创建 systemd 服务:
```ini
[Unit]
Description=UART to Network Service
After=network.target

[Service]
Type=simple
ExecStart=/opt/uart_service/uart_service
Restart=always

[Install]
WantedBy=multi-user.target
```

### 8.6 固件打包

使用 Python 脚本打包固件:
```bash
cd upgrade&&pack
python firmware_packer.py input.bin output.pkg
```

---

## 9. 开发指南

### 9.1 代码规范

#### 命名约定
- **文件名**: 小写，下划线分隔 (如 `app_dev.c`)
- **函数名**: 小写，下划线分隔 (如 `dev_config_init()`)
- **宏定义**: 大写，下划线分隔 (如 `MAX_ALIAS_LEN`)
- **结构体**: 大驼峰 (如 `SystemConfiguration`)
- **变量**: 小写，下划线分隔 (如 `g_system_config`)

#### 注释规范
- **文件头**: Doxygen 格式，包含文件名、描述、作者
- **函数注释**: @brief, @param, @return
- **行注释**: // 单行注释
- **块注释**: /* */ 多行注释

### 9.2 调试技巧

#### 9.2.1 日志输出
```c
LOG_DEBUG("Debug message: %d", value);
LOG_INFO("Info message: %s", str);
LOG_WARN("Warning message");
LOG_ERROR("Error: %s", strerror(errno));
```

#### 9.2.2 编译调试版本
```bash
make DEBUG=1  # 生成调试信息和 DEBUG 宏
```

#### 9.2.3 GDB 调试
```bash
# 在目标板上
gdb ./uart_service
(gdb) break main
(gdb) run
(gdb) backtrace
```

#### 9.2.4 网络抓包
```bash
# 抓取 TCP 5000 端口数据
tcpdump -i eth0 port 5000 -w capture.pcap

# 分析 Wireshark
wireshark capture.pcap
```

### 9.3 常见问题

#### Q1: 端口被占用
```
Error: Address already in use
解决：检查是否有旧进程运行
$ netstat -tlnp | grep :5000
$ kill -9 <PID>
```

#### Q2: Flash 写入失败
```
FLASH_ERROR_ADDR: 地址越界
解决：检查 Flash 地址范围定义
确认 config_data/目录存在且有写权限
```

#### Q3: 线程创建失败
```
Failed to create thread: Resource temporarily unavailable
解决：检查系统资源限制
$ ulimit -u  # 查看最大线程数
```

### 9.4 性能优化

#### 9.4.1 编译优化
```bash
make DEBUG=0  # -O2 优化
```

#### 9.4.2 内存管理
- 使用环形缓冲区减少内存分配
- 预分配静态内存池
- 避免频繁 malloc/free

#### 9.4.3 网络优化
- 使用非阻塞 I/O
- select/poll多路复用
- TCP keepalive 机制

### 9.5 扩展开发

#### 添加新功能模块
1. 在 `APP/` 目录创建新文件
2. 在 Makefile 中添加源文件
3. 在 `app_init.c` 中调用初始化
4. 在 `app_task_creation.c` 中创建任务

#### 修改端口配置
编辑 `APP/inc/app_com.h`:
```c
#define TCP_SETTING_PORT 4000  // 修改配置端口
#define UDP_SEARCH_PORT 48899  // 修改搜索端口
```

#### 增加串口通道数
编辑 `APP/inc/app_dev.h`:
```c
#define NUM_PORTS 32  // 从 16 扩展到 32
```

### 9.6 测试方法

#### 单元测试
```bash
# UART 测试
make uart_test
./uart_test
```

#### 集成测试
1. 启动服务：`./uart_service`
2. UDP 搜索测试：发送"SEARCH_DEVICE_WQ"
3. TCP 配置测试：连接端口 5000
4. 固件升级测试：连接端口 19001

#### 压力测试
```bash
# 多客户端并发连接
for i in {1..10}; do
  nc localhost 5000 &
done
```

---

## 附录

### A. 缩略语
- **UART**: Universal Asynchronous Receiver/Transmitter
- **HAL**: Hardware Abstraction Layer
- **SAL**: System Abstraction Layer
- **RealCOM**: Real COM Port Mode
- **LCM**: Liquid Crystal Module
- **CRC**: Cyclic Redundancy Check
- **FIFO**: First In First Out

### B. 参考文档
- POSIX 线程标准：IEEE Std 1003.1
- 16550 UART 规格书
- ARM Linux 交叉编译指南
- Socket 网络编程指南

### C. 版本历史
- **v1.0.0**: 初始版本，基本功能完成
- **v1.0.1**: 修复 Bug，优化性能
- **当前版本**: v1.0.1 (FW_VERSION: 01.00.00.01)

### D. 联系方式
- **技术支持**: support@example.com
- **项目仓库**: https://github.com/example/uart-service

---

**文档生成时间**: 2026-03-18  
**文档版本**: v1.0  
**适用软件版本**: v1.0.1
