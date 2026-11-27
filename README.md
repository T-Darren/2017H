# 2017H

基于 STM32H743VGT6 的信号发生与检波采集工程：AD9959 CH2 输出正弦波，经外部放大器、乘法器平方检波与低通滤波后，使用 ADS1256 采集直流电压，通过 USART2 控制和查看数据。

## 工程文件

- `2017H.ioc`：STM32CubeMX 工程及引脚配置。
- `Core/`：应用代码、AD9959 / ADS1256 驱动和串口命令。
- `Drivers/`：工程使用的 CMSIS 与 STM32H7 HAL 驱动，随仓库提供。
- `cmake/`、`CMakeLists.txt`、`CMakePresets.json`：构建配置。
- `.vscode/`、`.settings/`：VS Code STM32 开发配置。
- [DDS 接线与命令](README_AD9959.md)、[ADC 接线与命令](README_ADS1256.md)：硬件及串口使用说明。

## 环境与编译

当前使用 STM32CubeMX 6.16.1、STM32CubeCLT 1.20.0、ARM GCC、CMake 和 Ninja。先将 ARM GCC、CMake、Ninja 的可执行文件目录加入终端 PATH，然后在工程根目录运行：

```powershell
cmake --preset Debug
cmake --build --preset Debug --parallel 8
```

生成文件位于 `build/Debug/2017H.elf`、`2017H.hex`、`2017H.bin`。BIN 下载起始地址为 `0x08000000`；HEX 和 ELF 已包含地址信息。

本机 STM32CubeCLT 安装在 `D:/STM32CubeCLT_1.20.0`。队友安装路径不同时，需要调整 `.vscode/settings.json` 和 `.vscode/tasks.json` 中的工具路径。`build/` 包含本机构建缓存和备份，不纳入 Git；每台电脑首次克隆后独立配置和编译。

## VS Code 烧录

用 VS Code 打开工程文件夹，执行 `Tasks: Run Task` → `STM32: Build and Flash (ST-LINK)`。任务依次配置 Debug、编译、通过 ST-LINK 下载并校验 `2017H.hex`，最后复位运行。

仅编译时执行 `STM32: Build 2017H`。首次打开或移动本地工程目录后，若 CMake 提示缓存路径不一致，执行 `CMake: Delete Cache and Reconfigure`，或在工具链 PATH 配置完成后运行 `cmake --fresh --preset Debug`。

## 串口快速测试

USART2 使用 PA2 TX / PD6 RX，参数为 **115200、8N1、无流控**。USB-TTL 使用 3.3V 逻辑电平，TX/RX 交叉连接并共地；每条命令追加回车或换行。

```text
STATUS
SET 100kHz 512 0
ADC STATUS
ADC ON
ADC OFF
```

DDS 默认硬件 CH2、100kHz、幅度码 512、相位 0°；幅度码范围 0～1023，实际输出电压需用示波器确认。ADC 默认芯片 AIN1 对 AINCOM，100SPS、PGA=1，SYNC=PC6、RST=PC7。`ADC ON/OFF` 控制周期性串口显示，后台采集继续运行。

ADC 默认换算参考值为 2500mV，下载新固件后上电生效；旧固件可先用 `ADC VREF 2500` 临时调整。2026-11-27 加入本次实测的初步直流校准：`U(mV)=(U_UNCAL(mV)-9.615)×1.004039`，仅在 AIN1、PGA=1、100SPS、2500mV 参考时启用，`ADC STATUS` 显示 `CAL=ON`。串口保留原始 `RAW` 和未补偿电压 `U_UNCAL`；202mV 独立验证点校准后约 200.888mV，仍有约 -1.112mV 偏差，尚未完成全量程精度验证。`U` 是检波低通后的电压，尚未反算原高频信号幅值或功率，采集未加入软件平均。系数、适用条件及复核方法见 README_ADS1256.md。

2026-11-27 用户反馈此前 40MHz 检波采集时的抖动已消除。具体处理原因未记录，本次未独立进行硬件复测，不将其归因于上述直流校准。

## 协作流程

```powershell
git clone https://github.com/T-Darren/2017H.git
cd 2017H
git switch -c feature/your-change
```

修改前同步 `main` 的最新内容；在个人分支完成改动并编译后，推送分支，在 GitHub 创建 Pull Request 供队友检查后合并。私有仓库需要仓库所有者先在 GitHub 的 Settings → Collaborators 中添加队友，队友接受邀请后才能克隆和推送。

CubeMX 重新生成时启用保留 USER CODE，保留顶层 CMake 中四个自定义驱动与串口源文件，以及固件导出命令。提交源代码、`.ioc`、驱动与必要配置，避免提交本机缓存、账号凭据和编译目录。
