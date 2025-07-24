# Espressif 物联网 WebAssembly 虚拟机开发框架（Preview）

* [English Version](./README.md)

## 1. 简介

ESP-WASMachine 是面向物联网应用的 WebAssembly 虚拟机开发框架，对应的 WebAssembly 应用程序开发框架请参考 [README](https://github.com/espressif/esp-wdf/README_CN.md)。系统支持的主要功能模块和整体架构如下图所示：

<div align="center"><img src="docs/_static/esp-wasmachine_block_diagram.png" alt ="ESP-WASMachine Block Diagram" align="center" /></div>

### 1.1 目录

ESP-WASMachine 的主要目录结构如下：

```
esp-wasmachine/
    ├──components
        ├──data_sequence                    数据序列，用于虚拟机和应用之间的参数传递
        ├──extended_wasm_vfs                基于虚拟文件系统的硬件驱动
                ├──src
                    ├──wm_ext_vfs.c         硬件设备驱动初始化程序，依赖于 esp-iot-solution 里面的 extended_vfs
                    ├── ...
        ├──extended_wasm_native             WebAssembly Native API
            ├──src
                ├──wm_ext_wasm_native_mqtt.c    WebAssembly Native MQTT API
                ├──wm_ext_wasm_native.c     WebAssembly Native API 驱动初始化程序
                ├── ...
        ├──shell                            Shell 命令程序
            ├──src
                ├──shell_iwasm.c            iwasm 命令
                ├──shell_init.c             Shell 命令初始化程序
                ├── ...
        ├──wasmachine                       WAMR 软件平台适配和扩展程序 
            ├──src
                ├──wm_wamr_app_mgr.c        App Manager 适配和启动程序
                ├──wm_wamr.c                WAMR Heap 适配程序
    ├──main
        ├──fs_image                         默认使用的文件系统根目录
            ├──hello_world.wasm             基于 WebAssembly 的应用程序
        ├──wm_main.c                        系统初始化和启动程序
```

## 2. 安装开发环境

从实现原理来看，ESP-WASMachine 是基于 ESP-IDF 的应用程序，所以需要安装 ESP-IDF 的开发环境，相关流程请参考 ESP-IDF [文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html#id1)。

支持的 ESP-IDF 版本有 v5.1.x, v5.2.x, 5.3.x, 5.4.x, 5.5.x 和 master，相关版本如下：

- [v5.1.6](https://github.com/espressif/esp-idf/tree/v5.1.6)
- [v5.2.5](https://github.com/espressif/esp-idf/tree/v5.2.5)
- [v5.3.3](https://github.com/espressif/esp-idf/tree/v5.3.3)
- [v5.4.2](https://github.com/espressif/esp-idf/tree/v5.4.2)
- [v5.5-rc1](https://github.com/espressif/esp-idf/tree/v5.5-rc1)
- [master](https://github.com/espressif/esp-idf/tree/master)

支持的开发板有：

- [ESP32-DevKitC](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32/esp32-devkitc/user_guide.html#id1)

- [ESP32-S3-BOX](https://github.com/espressif/esp-box/blob/v0.3.0/docs/hardware_overview/esp32_s3_box/hardware_overview_for_box_cn.md)

- [ESP32-S3-BOX-Lite](https://github.com/espressif/esp-box/blob/v0.3.0/docs/hardware_overview/esp32_s3_box_lite/hardware_overview_for_lite_cn.md)

- [ESP32-S3-DevKitC](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)

- [ESP32-C6-DevKitC](https://docs.espressif.com/projects/espressif-esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html)

- [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-function-ev-board/index.html)

为了远程管理 WebAssembly 应用程序，还需要编译生成 `host_tool`，但是 `host_tool` 当前只支持在 Linux 操作系统上编译和使用。相关编译过程如下：

```
git clone -b WAMR-1.3.2 https://github.com/espressif/wasm-micro-runtime.git
cd wasm-micro-runtime/test-tools/host-tool
mkdir build
cd build
cmake ..
make
```

通过命令 `ls` 可以看到在当前目录编译生成的文件 `host_tool`：

```sh
ubuntu > ls
CMakeCache.txt  CMakeFiles  cmake_install.cmake  host_tool  Makefile
```

## 3. 工具介绍

### 3.1 命令行工具

ESP-WASMachine 集成命令行工具。方便您开发时进行调试，支持的命令有：

#### 3.1.1 iwasm

从文件系统加载 WebAssembly 应用程序并运行, 命令格式如下：

```
iwasm <file> <args> [配置参数]

    file: 带路径的 WebAssembly 应用程序名
    args: WebAssembly 应用程序运行参数
```

配置参数说明如下：

```
    -s/--stack_size: WebAssembly 应用程序栈的大小，单位字节
    -h/--heap_size:  WebAssembly 应用程序堆的大小，单位字节
    -e/--env:        WebAssembly WASI 应用程序的环境变量，多个变量之间用符号 "," 隔开，例如
                        单个变量：--env=\"key1=value1\"
                        多个变量：--env=\"key1=value1\",\"key2=value2\",...
    -d/--dir:        WebAssembly WASI 允许应用程序访问的目录，多个目录之间用符号 "," 隔开，例如
                        单个目录：--dir=<dir1>
                        多个目录：--dir=<dir1>,<dir2>,...
    -a/--addr-pool:  WebAssembly WASI 允许应用程序访问的对端网络地址，多个地址之间用符号 "," 隔开，例如
                        单个地址：--addr-pool=1.2.3.4/15
                        多个地址：--addr-pool=1.2.3.4/15,2.3.4.5/16,...
```

其中 `-e/--env`，`-d/--dir` 和 `-a/--addr-pool` 只有在使能 Libc WASI 时使用，Libc WASI 的配置项为 WAMR_ENABLE_LIBC_WASI，参考命令如下：

非 Libc WASI 模式：

```sh
iwasm wasm/demo.wasm -s 262144 -h 262144
```

Libc WASI 模式：

```sh
iwasm wasm/demo.wasm -s 262144 -h 262144 -e \"key1=value1\" -a 1.2.3.4/15
```

#### 3.1.2 ls

显示目录下的文件，默认显示当前目录下的文件，命令格式如下：

```
ls <file_or_directory>

    file_or_directory: 带路径的目录名
```

#### 3.1.3 free

显示内存的堆信息，包括全部堆信息、已使用堆信息以及剩余堆信息。使能 PSRAM 时，能分别显示 DRAM 和 PSRAM 的内存堆信息，命令格式如下：

```
free
```

#### 3.1.4 sta

配置 Wi-Fi Station 连接的目标 AP 的 SSID 和 password，并启动连接，命令格式如下：

```
sta -s <SSID> -p <password>
```

#### 3.1.5 install

安装文件系统里面的 WASM 应用程序:

```
install <file> [配置参数]

    file: 带路径的 WebAssembly 应用程序名
```

配置参数说明如下：

```
	-i: WebAssembly 应用程序名字
	--heap: WebAssembly 应用程序堆空间大小
	--type: WebAssembly 应用程序类型
	--timer: WebAssembly 应用程序可以使用的 timer 数量
	--watchdog: WebAssembly 应用程序看门狗间隔，单位是毫秒
```

#### 3.1.6 uninstall

卸载 WASM 应用程序:

```
uninstall [配置参数]
```

配置参数说明如下：

```
	-u: WebAssembly 应用程序名字
	--type: WebAssembly 应用程序类型
```

#### 3.1.7 query

获取 WASM 应用程序信息:

```
query [配置参数]
```

配置参数说明如下：

```
	-q: WebAssembly 应用程序名字，如果不带 `-q <app name>` 则获取所有 app 的信息
```

### 3.2 应用管理工具

WebAssembly 远程应用程序管理工具 [host_tool](https://github.com/bytecodealliance/wasm-micro-runtime/tree/main/test-tools/host-tool)，是 wasm-micro-runtime(WAMR) 自带的工具，可以通过 TCP/UART（当前只使用 TCP）与硬件设备通信，来实现在设备上远程安装/卸载 WebAssembly 应用程序。主要的命令格式如下：

```
./host_tool -i/-u/-q <app name> [配置参数]

    -i: 安装应用程序
    -u: 卸载应用程序
    -q: 获取应用程序信息
```

配置参数说明如下：

```
    -f: 带路径的 WebAssembly 应用程序文件名
    -S: 硬件设备的 IP 地址
    -P: 硬件设备上运行的 App Manager 程序使用的 TCP 端口号
```

您可以执行 `./host_tool` 来查看其他命令格式和参数。

## 4. 编译工程并运行

首先在 ESP-IDF 根目录下配置开发环境，相关命令如下：

```
. ./export.sh
```

接着切换到 ESP-WASMachine 目录下并执行 ESP-IDF 提供的命令来进行配置/编译/下载/调试，相关流程如下：

### 4.1 配置系统

1. 编译 ESP32-DevKitC 开发板固件:

```sh
idf.py set-target esp32
idf.py build
```

2. 编译 ESP32-S3-BOX 开发板固件:

```sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.esp-box" set-target esp32s3
idf.py build
```

3. 编译 ESP32-S3-DevKitC 开发板固件:

```sh
idf.py set-target esp32s3
idf.py build
```

4. 编译 ESP32-C6-DevKitC 开发板固件:

```sh
idf.py set-target esp32c6
idf.py build
```

5. 编译 ESP32-P4 开发板固件:

```sh
idf.py set-target esp32p4
idf.py build
```

6. 编译 ESP32-P4-Function-EV-Board 开发板固件:

```sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.esp32_p4_function_ev_board" set-target esp32p4
idf.py build
```

* 注意：ESP32-DevKitC 4MB flash 开发板 和 ESP32-C6-DevKitC 4MB flash 开发板均使用 partitions.4mb.single_app.csv 作为 partition table 文件。

### 4.2 烧录文件系统

默认启动需要挂载 littleFS 文件系统，所以需要烧录 littleFS image，否则系统启动会失败，相关命令如下：

```
idf.py storage-flash
```

烧录的 image 由 `main/fs_image` 生成，所以会包含 `main/fs_image` 目录下的子目录和文件，您可以把需要的文件放到该目录下并烧录。

- **注**：重新烧录文件系统 image 之后，flash 里面之前的文件系统存储的数据就会被覆盖。

### 4.3 编译及下载

```
idf.py build flash
```

按照上面的操作流程下载完所有的固件之后，执行命令 `idf.py monitor` 启动串口工具，显示启动信息。出现下面的 log 表示启动成功，进入命令行控制界面：

```
Type 'help' to get the list of commands.
Use UP/DOWN arrows to navigate through command history.
Press TAB when typing command name to auto-complete.
WASMachine>
```

更多编译和调试相关的命令可以参考 ESP-IDF [文档](https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32s3/api-guides/build-system.html#idf-py)。

### 4.4 运行 WebAssembly 应用程序

输入命令 `ls wasm`，可以看到存储在 littleFS image 里面位于 `wasm` 目录下的 WebAssembly 应用程序 `hello_world.wasm`，执行命令 `iwasm wasm/hello_world.wasm` 运行固件，显示如下 log 信息表示运行成功：

```
Hello World!
```

您可以参照 [相关说明](#2-烧录文件系统) 烧录自己的 WebAssembly 应用程序到文件系统。

### 4.5 远程安装/卸载 WebAssembly 应用程序

TCP 端口号在配置项 WASMACHINE_TCP_SERVER 中定义，默认值是 8080，您可以在下列的菜单中修改 TCP 端口号：

```
WASMachine Configuration  --->
    Generic  --->
        [*] Enable WAMR APP Management
        [*]     Enable TCP server
        (8080)      TCP Port
```

运行 `host_tool` 工具安装/卸载/查看 WebAssembly 应用程序，具体命令可以参考[相关说明](#1.3-应用管理工具)。

默认配置下，设备最多只支持安装 3 个应用程序。

#### 4.5.1 连接 AP

输入如下命令（请将 `myssid` 和 `mypassword` 替换为您的 SSID 和密码）。为实现远程安装/卸载 WebAssembly 应用程序，请确保 PC 和硬件设备在同一个 AP 网络中：

```
WASMachine> sta -s myssid -p mypassword
```

如出现下面的 log，表示连接 AP 并成功获取 IP 地址（log 中的 IP 地址应为您的 IP 地址）：

```
I (158337) esp_netif_handlers: sta ip: 172.168.30.182, mask: 255.255.255.0, gw: 172.168.30
```

#### 4.5.2 安装

执行以下的命令安装本地的 `hello_world.wasm` 到硬件设备上，程序名称为 `app0`，其他操作需要使用该名称：

```
cd wasm-micro-runtime
./test-tools/host-tool/build/host_tool \
    -i app0 \
    -f main/fs_image/wasm/hello_world.wasm \
    -S 172.168.30.182 \
    -P 8080
```

返回以下信息表示安装成功：

```
response status 65
```

#### 4.5.2 查看

执行以下的命令获取 `app0` 的信息：

```
cd wasm-micro-runtime
./test-tools/host-tool/build/host_tool \
    -q app0 \
    -S 172.168.30.182 \
    -P 8080
```

返回以下 log 表示成功获取信息：

```
response status 69
{
        "num":  1,
        "applet1":      "app0",
        "heap1":        8192
}
```

#### 4.5.3 卸载

执行以下的命令卸载安装的 `app0`：

```
cd wasm-micro-runtime
./test-tools/host-tool/build/host_tool \
    -u app0 \
    -S 172.168.30.182 \
    -P 8080
```

返回以下信息表示卸载成功：

```
response status 66
```

## 5. 后续工作安排

ESP-WASMachine 是 WebAssembly 技术在 ESP32 系列芯片上的尝试，当前还有一些问题尚未解决，我们会尽力解决这些问题，并添加更多的丰富易用的功能。通过此基础版本，我们的目标是帮助您更加方便地开发 WebAssembly 虚拟机程序、添加新的扩展和部署应用程序。

如果您有基于此框架的想法或需求，或者有兴趣探索此类问题，欢迎您与我们进行沟通。
