# init/ 架构说明

## 定位

`init/` 是 本子树架构的统一启动层。它把分散在各个 `.cpp` 中的初始化函数、线程启动函数和 CAN 接收处理项，通过链接段收集成表，再按固定阶段执行。

## 运行路径

```text
startup/main.cpp
    -> System_Startup()
        -> RunStage(InitStage)
        -> RunStage(ThreadStage)
            -> __user_init_start/end
            -> __user_thread_start/end
```

当前阶段顺序为：

```text
PreInit -> PreThread
EarlyInit -> EarlyThread
MidInit -> MidThread
LateInit -> LateThread
AppInit -> AppThread
```

每一对阶段中先初始化资源，再启动使用这些资源的线程。

## 注册表

`init/startup/Init_entry.hpp` 定义：

- `REGISTER_INIT(fn, stage, level, fail, cleanup, name)`：写入 `.user_init`；
- `REGISTER_THREAD(fn, stage, name)`：写入 `.user_thread`；
- `InitStage`、`ThreadStage`：表达执行阶段；
- `InitLevel`：同阶段内执行顺序（High → Mid → Low）+ 失败处理等级；
- `InitFailPolicy`：失败后停机（`HaltOnFail`）还是继续（`ContinueOnFail`）；
- `cleanup`：初始化失败时回滚本项已申请的资源，可为 `nullptr`。

拥有实现的组件在自己的 `.cpp` 中注册，不需要修改中央启动器。

## 失败语义

`InitFunc` 返回 `bool`。`RunStage()` 发现失败后交给 `HandleInitFail()`：

| 等级 | 执行顺序 | 失败行为 |
| --- | --- | --- |
| `High` | 第一轮 | 记录错误 + 蜂鸣 + 停机 |
| `Mid` | 第二轮 | 记录警告，继续启动 |
| `Low` | 第三轮 | 记录警告，继续启动 |

阶段决定什么时候运行，等级决定同阶段内的先后顺序与失败策略。实际失败行为由 `InitFailPolicy` 决定，`HandleInitFail()` 只区分停机与继续两类。

初始化失败时启动器**先调该条目的 `cleanup`**（支持部分初始化状态），再走失败策略；`HaltOnFail` 停机前还会**逆序回滚所有已成功项**，把硬件放回安全态（PWM 停输出、Flash 掉电等），最后长鸣并进入休眠停机循环。

线程启动项没有等级，但带 `InitFailPolicy`：启动函数返回 `false` 时，`HaltOnFail` 打印 `[boot] xxx start fail, halting` 并置 `startup_aborted`，后续所有阶段不再执行；`ContinueOnFail` 打印 `[boot] xxx start fail, continue` 后继续启动其它线程。

`System_Startup()` 保证返回，停机策略由 `main.cpp` 查询 `System_StartupAborted()` 后决定。

## 启动期耗时

`REGISTER_INIT()` / `REGISTER_THREAD()` 每次展开都会额外生成一个 `BootTimingSlot`，放进 `.boot_timing` 段。启动器在执行每项前后读 `k_cycle_get_64()`，把耗时与结果写入槽。

- 段定义在 `cmd/linker/boot_timing.ld`，用 `DATA_SECTIONS`（需可写）
- 槽数量由链接器按实际注册项决定，无固定数组
- 查询入口：`boot timing` 命令（按耗时降序，失败项标 `FAIL`）

## 链接段

`init/CMakeLists.txt` 纳入 `startup/Init_entry.cpp`、`startup/boot_timing.cpp`、`startup/main.cpp`、`irq/Irq_handlers.cpp`，并引入两个链接脚本：

```cmake
zephyr_linker_sources(ROM_SECTIONS  ../cmd/linker/dust_init.ld)     # 只读注册表
zephyr_linker_sources(DATA_SECTIONS ../cmd/linker/boot_timing.ld)   # 可写耗时槽
```

`dust_init.ld` 保留：

```text
.user_init
.user_thread
.can_rx1/.can_rx2/.can_rx3
.remote
.imu
.shell_var
```

注册表存在不等于硬件已初始化成功。外设设备状态、过滤器、`can_start()` 和 IRQ 使能仍由驱动路径负责。

## 边界

| `init/` 负责 | `init/` 不负责 |
| --- | --- |
| 统一入口和阶段遍历 | 具体设备协议 |
| 注册项的执行顺序 | 当前机器人业务策略 |
| 初始化失败等级处理 | 代替驱动检查硬件状态 |
| 公共 CAN RX 表分发 | 为每个模块创建线程 |

## 维护规则

- 初始化函数写在拥有资源的 `.cpp` 中。
- 资源准备使用 `REGISTER_INIT()`，线程循环使用 `REGISTER_THREAD()`。
- 新增功能优先增加本地注册项，不扩大 `System_Startup()`。
- 修改链接段时必须同时检查注册宏、CMake 和链接符号。

---

## 目录和文件职责

`init/` 当前只承载启动框架和公共分发机制：

| 文件 | 职责 |
| --- | --- |
| `startup/Init_entry.hpp` | 阶段、等级、`InitEntry` / `ThreadEntry` 结构与注册宏 |
| `startup/Init_entry.cpp` | 启动顺序、阶段遍历、失败处理与回滚 |
| `startup/System_startup.h` | 对 C 入口暴露 `System_Startup()` / `System_StartupAborted()` |
| `startup/boot_timing.hpp` | `BootTimingSlot` 结构与耗时槽写入/遍历接口 |
| `startup/boot_timing.cpp` | `.boot_timing` 段边界遍历实现 |
| `startup/main.cpp` | 系统入口：启动 + 中止检查 + 空转 |
| `thread/thread.hpp` | 线程模板：创建、暂停/恢复、受管任务外壳、运行统计 |
| `thread/thread_registry.hpp` | 已启动线程登记表 |
| `irq/Irq_handlers.h` | CAN RX 注册项和回调声明 |
| `irq/Irq_handlers.cpp` | CAN RX section 遍历和 ID 分发 |
| `CMakeLists.txt` | 编译启动实现、加入三个子目录 include、引入链接脚本 |
| `README.md` | 使用说明、边界和维护流程 |
| `ARCHITECTURE.md` | 运行模型、链接关系和架构约束 |

启动框架不拥有 IMU、遥控器、电机或底盘对象。具体组件在自己的源文件
中注册，`init/` 只负责在统一规则下找到并调用它们。

## 依赖边界

```text
startup/main.cpp
      ↓
init/System_Startup()
      ↓
project / modules / drivers 的本地注册项
```

`startup/main.cpp` 只需要进入 `System_Startup()`。`init/` 可以依赖注册项所需
的公共 RTOS、链接符号和蜂鸣器提示能力，但不应反向包含某个具体项目线程
或设备协议。

其他层与 `init/` 的关系如下：

| 层 | 提供给 `init/` 的内容 | `init/` 不应做的事 |
| --- | --- | --- |
| `project/` | 本地初始化、线程和 CAN RX 注册 | 不替项目决定业务策略 |
| `modules/` | 设备拥有的初始化或专用 section 注册 | 不解析模块协议 |
| `drivers/` | 设备状态和 IRQ 所需的底层能力 | 不代替驱动检查硬件 |
| `topic/` | 启动后线程使用的数据契约 | 不参与启动阶段调度 |
| `cmd/` | 链接脚本和调试入口 | 不把业务组件塞进启动器 |
| `startup/main.cpp` | 唯一的系统入口调用 | 不保存项目初始化列表 |

## 三个时间点

启动问题必须区分三个时间点：

### 编译配置时间

`Kconfig` 决定功能是否被选择，`CMake` 决定源文件是否进入编译。没有
源文件就不会有注册对象，也不会产生对应的 section 内容。

### 链接时间

注册宏把 `InitEntry` 放入 `.user_init`、`.user_thread` 或其他专用 section。
链接脚本通过 `KEEP()` 保留这些 section，并导出 start/end 符号。

### 运行时间

`System_Startup()` 按阶段调用 `RunStage()` 和线程阶段遍历。函数返回值、
设备 ready 状态、总线启动和 IRQ 是否有效，都只能在运行时确认。

```text
CONFIG_* / CMake
        ↓
编译出的注册对象
        ↓
链接段和 start/end 符号
        ↓
阶段遍历
        ↓
初始化函数和线程函数
```

这三层证据不能互相替代。尤其是“section 中有回调”不等于“硬件一定
收到了数据”。

## 阶段模型

### 初始化阶段

`InitStage` 目前包含：

```text
PreInit
EarlyInit
MidInit
LateInit
AppInit
```

### 线程阶段

`ThreadStage` 对应：

```text
PreThread
EarlyThread
MidThread
LateThread
AppThread
```

线程阶段和初始化阶段是两组命名空间。`REGISTER_THREAD()` 使用同一个
注册项结构保存 stage 字段，并由线程阶段遍历函数处理；不要把线程阶段
直接当作一次性初始化阶段使用。

### 等级

`InitLevel` 目前为 `High`、`Mid`、`Low`。阶段决定何时执行，等级决定
失败后的策略：

| 等级 | 失败行为 | 适用内容 |
| --- | --- | --- |
| `High` | 停止后续启动或进入故障处理 | 系统运行所需的基础资源 |
| `Mid` | 报错后继续 | 可降级的核心功能 |
| `Low` | 警告后继续 | 可选、实验或辅助功能 |

阶段和等级互相独立。一个 `AppInit` 项仍然可以是 `High`，一个早期阶段
的可选 LED 初始化也可以是 `Low`。

## InitEntry 和本地注册

`InitEntry` 的核心字段是：

```text
function  要调用的 InitFunc
cleanup   失败回滚与清理回调（可为 nullptr）
stage     初始化阶段
level     同阶段执行顺序 + 失败等级
fail      失败策略：停机 / 继续
name      日志和诊断名称
```

`ThreadEntry` 是独立结构，字段是：

```text
start  线程启动函数（创建线程后立即返回）
stage  线程启动阶段
name   日志和诊断名称
```

`InitFunc` 返回 `bool`。注册项通常应该是静态函数或拥有稳定生命周期的
函数，不应把临时对象成员地址放进链接段。

注册宏的作用是把“代码所在位置”和“启动顺序信息”绑定起来：

```text
REGISTER_INIT(...)
        → .user_init

REGISTER_THREAD(...)
        → .user_thread
```

这种设计让组件在自己的 `.cpp` 中声明启动项，新增功能不需要修改一个
中心数组，也不需要修改 `System_Startup()` 的固定骨架。

注册名应能说明组件和动作，便于启动日志和 map 文件定位。阶段、等级和
资源依赖应与实际代码保持一致，不能因为复制模板而沿用错误的 stage。

## System_Startup 执行顺序

当前启动器按固定阶段交替执行初始化项和线程项：

```text
PreInit
  ↓
PreThread
  ↓
EarlyInit
  ↓
EarlyThread
  ↓
MidInit
  ↓
MidThread
  ↓
LateInit
  ↓
LateThread
  ↓
AppInit
  ↓
AppThread
```

当前实现会在阶段之间加入约 500 ms 的等待，并使用短蜂鸣提示部分阶段
完成。等待和提示属于启动器的节奏与诊断行为，不是组件依赖表达方式。
组件不应依赖某个固定延时来判断其他设备已经 ready，应使用接口状态或
显式检查。

当前项目的主要注册分布如下：

| 组件 | InitStage | ThreadStage | Level |
| --- | --- | --- | --- |
| GPIO 输出 | `PreInit` | `PreThread` | `Low` |
| 遥控器 | `PreInit` | `PreThread` | `High` |
| CAN TX | `PreInit` | `PreThread` | `High` |
| GPIO 输入 | `EarlyInit` | `EarlyThread` | `Low` |
| IMU | `EarlyInit` | `LateThread` | `High` |
| 底盘 | `MidInit` | `MidThread` | `Mid` |
| 云台 | `MidInit` | `MidThread` | `Mid` |
| PC 通信 | `PreInit` | `LateThread` | `High` |
| 测试 | `LateInit` | `LateThread` | `High` |

这张表是当前项目的运行装配，不是 `init/` 对所有应用的硬编码要求。

## 初始化函数的契约

一个初始化函数只应承诺自己负责的资源已经进入可用或明确失败状态：

- 初始化成功返回 `true`；
- 初始化失败返回 `false`，不通过“打印一句日志后仍返回 true”隐藏失败；
- 不在初始化函数中建立不可控的无限循环；
- 对外设 ready、校准、总线启动等状态使用明确检查；
- 对可恢复的设备离线状态交给线程或模块状态机处理；
- 对不可恢复的基础资源失败使用合适的 `InitLevel`。

阶段只能表达粗粒度顺序。例如 IMU 线程排在云台线程之后，并不自动保证
IMU 已经输出有效姿态；云台线程仍应检查姿态数据是否 ready、是否超时。

## 链接脚本和 section

`init/CMakeLists.txt` 编译 `startup/Init_entry.cpp`、`irq/Irq_handlers.cpp`，并引入
`cmd/linker/dust_init.ld`。链接脚本保留的主要 section 为：

```text
.user_init
.user_thread
.can_rx1
.can_rx2
.can_rx3
.remote
.imu
.shell_var
```

通用启动器只直接遍历 `.user_init` 和 `.user_thread`。其他 section 的
消费者不同：

| section | 消费者 | 作用 |
| --- | --- | --- |
| `.can_rx1/2/3` | `user_canN_rx_callback()` | 按 CAN ID 分发接收帧 |
| `.remote` | `Remote` 模块 | 收集遥控器协议实现 |
| `.imu` | IMU 模块 | 收集芯片或 IMU 实现 |
| `.shell_var` | shell 侧代码 | 收集调试变量或命令项 |
| `.boot_timing` | `BootTiming` / `boot` 命令 | 启动耗时槽，启动器写入 |

修改 section 名、段边界或 `KEEP()` 时，必须同时检查注册宏、CMake、导出
符号和消费者代码。只改其中一处会造成“编译通过但启动表为空”或“回调
注册但永远不被消费”。

## CAN RX 分发

CAN RX 不是初始化阶段遍历，而是独立的中断或驱动回调路径：

```text
CAN_RX_HANDLER(bus, id, handler)
        ↓
.can_rxN
        ↓
__can_rxN_start / __can_rxN_end
        ↓
user_canN_rx_callback()
        ↓
按 ID 线性查找
        ↓
handler(data)
```

`irq/Irq_handlers.h` 当前定义 CAN 总线编号、`CanRxHandler` 和
`CanRxEntry`。`irq/Irq_handlers.cpp` 对每个总线 section 做线性匹配，并在
找到第一个相同 ID 后调用处理函数。

该路径的职责边界是“把已经收到的帧分发给注册回调”。它不负责：

- 初始化具体 CAN 设备；
- 配置收发过滤器；
- 调用 `can_start()`；
- 确认 IRQ 已经连接并使能；
- 判断总线物理层是否有有效数据；
- 执行耗时控制计算。

因此，CAN 回调问题必须把 section 注册、驱动设备初始化、过滤器、IRQ
和物理总线分开验证。

## 与其他层的边界

| 层 | 依赖 `init/` 的方式 | `init/` 的边界 |
| --- | --- | --- |
| `project/` | 本地注册初始化和线程 | 不把项目业务写入启动器 |
| `modules/` | 注册设备实现或由项目调用 | 不由启动器解析设备协议 |
| `drivers/` | 提供设备和 IRQ 初始化 | 不由启动器代替硬件检查 |
| `topic/` | 供线程启动后交换数据 | 不参与 section 调度 |
| `cmd/` | 提供链接脚本和调试资源 | 不承载具体业务对象 |
| `startup/main.cpp` | 调用 `System_Startup()` | 不维护全局组件列表 |

## 新增初始化项

新增一个初始化或线程组件时，建议按以下流程：

1. 在拥有资源的模块或项目 `.cpp` 中定义函数。
2. 明确函数返回值、失败等级和 ready 语义。
3. 选择 `InitStage` 或 `ThreadStage`，不要用固定延时代替依赖。
4. 使用对应注册宏写入正确 section。
5. 确认源文件在 Kconfig 和 CMake 中可选且会被编译。
6. 如果使用专用 section，确认链接脚本 `KEEP()` 和消费者都存在。
7. 检查它与 topic、驱动状态、线程周期之间的依赖。
8. 用静态搜索确认注册宏、section 符号和调用路径一致。

如果新增内容属于设备协议、控制算法或业务策略，应放在对应层，再由
项目本地注册接入启动链。不要为了一个新组件在 `System_Startup()` 中
追加专门的函数调用。

## 验证顺序

`init/` 相关变更的最小静态验证顺序为：

```text
注册宏
  ↓
编译源文件
  ↓
链接 section
  ↓
start/end 符号
  ↓
阶段过滤
  ↓
函数返回值
  ↓
设备和 IRQ 状态
```

前五步可以通过源代码、Kconfig、CMake、链接脚本和 map 信息确认；最后
一步必须结合运行日志、驱动状态和真实硬件数据。不要用“看到了注册宏”
替代后续验证。
