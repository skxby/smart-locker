# S2-04 GNU Arm 构建

`scripts/build_firmware_gnu.py` 只构建独立SWD S204目标。使用外部已安装的 [xPack GNU Arm Embedded GCC 15.2.1-1.1](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/tag/v15.2.1-1.1)，不修改PATH/注册表、不自动下载、安装或烧录。Windows x64 ZIP SHA256固定为 `bae6a3d1667697ce750c3b13d6d26d80973ecedc2cc87bf04869e83447fd93ea`，下载与许可文件保留在用户工具目录，工具链不进入仓库。

```text
python -B scripts/build_firmware_gnu.py --target s204 --clock hse8_168 --toolchain-root C:/Users/%USER%/Documents/locker-hardware-tools/xpack-arm-none-eabi-gcc-15.2.1-1.1
python -B scripts/build_firmware_gnu.py --target s204 --clock hsi16 --toolchain-root C:/Users/%USER%/Documents/locker-hardware-tools/xpack-arm-none-eabi-gcc-15.2.1-1.1
```

构建复用原HAL、FreeRTOS V11.2.0、littlefs V2.11.2，完整检查旧 `dependencies.lock.json` 的254个文件SHA，不改原库和旧锁。原锁已包含 `portable/GCC/ARM_CM4F/port.c` 与 `portmacro.h`，直接使用，避免重复下载另一份FreeRTOS移植。FreeRTOS仍MIT，littlefs仍BSD-3-Clause。

CMSIS原快照缺GNU编译器头。`firmware/hal/cmsis-gcc/` 仅补 [ARM CMSIS_5 5.6.0固定提交](https://github.com/ARM-software/CMSIS_5/tree/b5f0603d6a584d1724d952fd8b0737458b90d62b) 的 `cmsis_gcc.h` 和Apache-2.0许可，不覆盖原CMSIS；官方URL、SHA及复用原锁的GNU port映射在 `gnu-supplement.lock.json`，每次构建再次验证补充文件SHA。没有复制不明许可驱动。

GNU编译使用C99、Cortex-M4/Thumb、FPv4-SP-D16 hard-float、-Os、函数/数据分节、newlib nano与nosys、链接gc-sections。`startup_gnu.S` 从锁定ST启动文件保留完整98项F407向量顺序，执行data复制、bss清零、SystemInit、初始化数组、main；SVC/PendSV/SysTick由原FreeRTOS GNU port实际实现，TIM6仍为HAL时基。GNU port对优先级常量使用编译器约束，当前FreeRTOSConfig无需改动。

`firmware/keil/stm32f407vg.ld` 定义Flash 0x08000000/1MiB、主SRAM 0x20000000/128KiB，排除CCM；初始MSP=0x20020000，静态预留主栈2KiB/库堆512B，FreeRTOS任务仍使用原16KiB静态heap_4。链接断言向量恰392B且RAM不溢出。未引入锁定依赖外的文件系统或运行时任务。

两时钟GNU构建已编译/链接退出0，每版39单元：HSE ROM34924B/RAM60064B，HSI ROM32648B/RAM60064B。日志包含原HAL三个unused-parameter警告及工具链libgcc `_dvmd_tls.o` 缺GNU-stack标记提示；未修改vendor源码消除警告。Cortex-M裸机不依赖主机ELF进程栈权限，该提示不构成已验证硬件防护。

初始MDK-Lite完整链接超32KiB的失败日志保留，不补丁工具、绕过授权或删减保护。GNU提供完整合法构建入口；构建通过不代表板上运行、Flash持久化或掉电验收通过。
