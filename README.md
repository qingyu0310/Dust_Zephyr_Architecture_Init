# init/ — 启动层

`init/` 是当前工程的统一启动层，负责把 `main()`、链接段注册表、初始化阶段、线程启动阶段和公共中断分发入口串起来。

它不属于某一个具体机器人项目，也不属于某个设备模块。它回答的问题是：

```text
镜像里已经被编译进来的组件，应该按什么顺序初始化和启动？
```

---

## 职责

`init/` 主要负责：

- 提供 `System_Startup()`，作为 `src/main.c` 调用的系统启动入口；
- 定义 `REGISTER_INIT()` 和 `REGISTER_THREAD()`；
- 遍历 `.user_init` 和 `.user_thread` 链接段；
- 按阶段执行初始化函数和线程启动函数；
- 根据 `InitLevel` 处理初始化失败；
- 提供 CAN RX 链接段分发入口；
- 将 `cmd/linker/tflm_init.ld` 接入 Zephyr 链接流程。

它不负责：

- 决定某个业务线程是否编译；
- 写具体 IMU、Remote、底盘或云台初始化逻辑；
- 直接访问 UART、CAN、SPI、GPIO 等外设；
- 保存模块运行状态；
- 实现算法或 topic 数据契约。

这些内容分别属于 `project/`、`modules/`、`drivers/`、`algorithm/` 和 `topic/`。

---

## 目录

```text
init/
├── CMakeLists.txt
├── Init_entry.hpp
├── Init_entry.cpp
├── System_startup.h
├── Irq_handlers.h
└── Irq_handlers.cpp
```

| 文件 | 作用 |
| --- | --- |
| `System_startup.h` | 对 C 入口暴露 `System_Startup()` |
| `Init_entry.hpp` | 定义启动阶段、失败等级、注册项结构和注册宏 |
| `Init_entry.cpp` | 遍历 `.user_init/.user_thread` 并执行启动阶段 |
| `Irq_handlers.h` | 定义 `CAN_RX_HANDLER()` 和 CAN RX 分发入口 |
| `Irq_handlers.cpp` | 根据 CAN ID 遍历 `.can_rx1/2/3` 并调用回调 |
| `CMakeLists.txt` | 编译启动层源文件并加入链接脚本 |

---

## 当前启动链

根入口位于：

```text
src/main.c
```

当前入口关系是：

```text
main()
    ->
System_Startup()
    ->
RunStage(...)
```

`main()` 不直接初始化 IMU、Remote、CAN、底盘或云台。具体组件是否存在，先由 Kconfig/CMake 决定；具体组件什么时候运行，再由 `init/` 按链接段表遍历。

---

## 阶段顺序

当前启动器按下面的顺序执行：

```text
PreInit
    ->
PreThread
    ->
EarlyInit
    ->
EarlyThread
    ->
MidInit
    ->
MidThread
    ->
LateInit
    ->
LateThread
    ->
AppInit
    ->
AppThread
```

每一组通常表达：

```text
先初始化对象和底层资源
    ->
再启动依赖这些资源的线程循环
```

示例：

| 阶段 | 适合放什么 |
| --- | --- |
| `PreInit` | 早期总线、调试控制台、基础输出 |
| `PreThread` | 早期通信线程或基础 IO 线程 |
| `EarlyInit` | 传感器、输入设备、需要较早准备的数据源 |
| `EarlyThread` | 传感器采样线程、输入线程 |
| `MidInit` | 底盘、云台等控制对象 |
| `MidThread` | 主要控制线程 |
| `LateInit` | 依赖前面能力的通信或上位机接口 |
| `LateThread` | 后期通信、调试或辅助线程 |
| `AppInit` | Demo、测试、推理示例等应用级入口 |
| `AppThread` | Demo、测试、推理线程 |

这张表是组织建议，不是强制依赖声明。真正的依赖关系仍应通过 Kconfig `select`、模块接口和文档写清楚。

---

## 注册宏

### REGISTER_INIT

初始化函数使用 `REGISTER_INIT()` 注册到 `.user_init`：

```cpp
static bool thread_init()
{
    return true;
}

REGISTER_INIT(thread_init, MidInit, Mid, "foo_init");
```

宏展开后会生成一个静态 `InitEntry`，并放入 `.user_init` 段。启动器运行时通过 `__user_init_start` 和 `__user_init_end` 遍历。

### REGISTER_THREAD

线程启动函数使用 `REGISTER_THREAD()` 注册到 `.user_thread`：

```cpp
static bool thread_start()
{
    return true;
}

REGISTER_THREAD(thread_start, MidThread, Mid, "foo_start");
```

这样可以把“对象初始化”和“线程启动”分开，避免线程启动时依赖的资源还没有准备好。

---

## 失败等级

每个启动项都有一个 `InitLevel`：

| 等级 | 当前行为 |
| --- | --- |
| `High` | 打印错误，触发错误蜂鸣并停机 |
| `Mid` | 打印告警，继续启动 |
| `Low` | 打印告警，继续启动 |

`InitLevel` 表示失败处理策略，不是同一阶段内的排序优先级。当前同阶段执行顺序由链接段中的条目顺序决定，因此不要把同阶段顺序当作强依赖机制。

如果组件之间存在硬依赖，优先用不同阶段表达；如果依赖仍然复杂，应在组件的 `thread_init()` 中显式检查依赖对象是否 ready。

---

## 链接段关系

`init/` 运行时会消费这些链接段边界：

| 链接段 | 注册宏 | 边界符号 |
| --- | --- | --- |
| `.user_init` | `REGISTER_INIT()` | `__user_init_start/end` |
| `.user_thread` | `REGISTER_THREAD()` | `__user_thread_start/end` |
| `.can_rx1` | `CAN_RX_HANDLER(USER_RX_CAN1, ...)` | `__can_rx1_start/end` |
| `.can_rx2` | `CAN_RX_HANDLER(USER_RX_CAN2, ...)` | `__can_rx2_start/end` |
| `.can_rx3` | `CAN_RX_HANDLER(USER_RX_CAN3, ...)` | `__can_rx3_start/end` |

这些 section 的实际定义当前集中在：

```text
cmd/linker/tflm_init.ld
```

`init/CMakeLists.txt` 通过：

```cmake
zephyr_linker_sources(ROM_SECTIONS ../cmd/linker/tflm_init.ld)
```

把链接段定义加入 Zephyr 链接流程。

---

## CAN RX 分发

CAN 接收回调不是在每个业务文件里手写总分发器，而是用 `CAN_RX_HANDLER()` 注册：

```cpp
CAN_RX_HANDLER(USER_RX_CAN1, 0x201, motor_rx_handler, motor_201);
```

注册项进入 `.can_rx1` 后，`user_can1_rx_callback()` 会按 CAN ID 遍历并调用对应处理函数。

这和 `REGISTER_INIT()` 的思路一致：

```text
业务文件本地声明
    ->
链接段收集
    ->
统一入口遍历
```

---

## 新增线程的接入方式

一个典型项目线程应该在自己的 `trd_xxx.cpp` 中完成注册：

```cpp
#include "Init_entry.hpp"

namespace thread::foo {

static bool thread_init()
{
    return Foo::Instance().Init();
}

static bool thread_start()
{
    Foo::Instance().Start();
    return true;
}

REGISTER_INIT(thread_init, MidInit, Mid, "foo_init");
REGISTER_THREAD(thread_start, MidThread, Mid, "foo_start");

}  // namespace thread::foo
```

新增线程通常还需要同步修改：

```text
project/thread/Kconfig
project/thread/CMakeLists.txt
project/boards/<vendor>/<board_cfg>/<board>.overlay
```

但不应该修改：

```text
src/main.c
init/Init_entry.cpp
```

也就是说，`init/` 提供启动框架，具体组件通过本地注册进入框架。

---

## 与其他层的边界

| 层 | 和 init 的关系 |
| --- | --- |
| `src/` | 只调用 `System_Startup()` |
| `project/` | 选择板级配置和业务线程，线程本地注册启动项 |
| `drivers/` | 提供外设访问能力，不直接决定启动阶段 |
| `modules/` | 提供设备能力，可被项目线程初始化 |
| `algorithm/` | 提供纯计算能力，通常不直接注册启动项 |
| `topic/` | 提供线程间数据契约，不参与启动调度 |
| `cmd/` | 可以注册调试控制面，也提供当前链接脚本位置 |

---

## 设计原则

- **main 保持干净**：`main()` 只进入系统启动器，不关心具体组件。
- **本地注册**：组件在自己的 `.cpp` 中声明启动项。
- **编译决定存在**：Kconfig/CMake 未选中的源文件不会贡献注册项。
- **运行决定顺序**：`init/` 运行时按阶段遍历链接段。
- **失败策略显式**：每个启动项声明自己的 `InitLevel`。
- **不隐藏强依赖**：阶段只能表达粗粒度顺序，复杂依赖要在接口和 ready 检查中写清楚。

