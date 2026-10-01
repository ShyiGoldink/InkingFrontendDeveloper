---
name: CodeStyleRule
description: 本项目的编码与解耦规范，提炼自 InkingBackendFramework（除 net 模块）。写新组件、新模块、改接口、动 CMake 之前先读它；配套 docs/AGENTS.md 是"这个仓库当前怎么跑"，本文件是"代码该怎么写"。
---

# 编码与解耦规范

适用范围：`InkingFrontendDeveloper` 与 `InkingBackendFramework` 的全部 C++ 代码。
来源：对后端框架（除 `net` 模块）实际写法的提炼，**优先照抄现成的正确写法**，
而不是重新发明一套。遇到与本规范冲突的既有代码，以本规范为准并在重构时改掉。

规范性用语：**必须**（违反就是 bug 或必然的返工）、**应该**（默认这么做，
有理由可以例外）、**可以**（可选）。

---

## 1. 分层与依赖方向

**必须**保持单向依赖：**上层可以依赖下层，下层绝不能反过来认识上层。**

后端的分层（`FrameworkAnalysis.txt` 里自己定的）就是判据：

```text
core       最基础生命周期：启动、停止、配置、错误码
log        日志
net        网络连接和收发            ← 本项目不参考
protocol   包头、消息 ID、payload
dispatch   根据消息 ID 分发到 handler
console    本地命令行管理
storage    数据库/文件抽象
```

前端的对应关系：

```text
basic/     锚点、时间线、日志          ← 谁都不依赖的底座
dataStruct/ 纯数据结构                 ← 只依赖标准库
thread/    任务队列、线程池
ui/        消息队列
scene/     场景、场景库、渲染树         ← 依赖 basic
window/    窗口、主循环                ← 依赖 scene
```

判据一句话：**如果 A 的头文件里出现了 B 的类型，那 B 就不能反过来在头文件里出现 A。**
出现环就说明分层错了，拆接口而不是加前向声明糊过去。

---

## 2. 头文件纪律

### 2.1 公开头里只放声明，不放依赖

**必须**做到"引入一个头文件，不该被迫拖进整个第三方库"。前端的硬性要求：

- 公开头里**不包含 `<SDL3/SDL.h>`**；需要类型时只做前向声明
- `SDL_Event` 是 `union`，前向声明**必须**写 `union SDL_Event;`（写 `struct` 会冲突）
- 用 `<cstdint>` 的 `std::uint32_t`，不要用 SDL 的 `Uint8`

后端同样是这个思路，只是形式不同——`CommandLibrary.h` 的注释写得很直白：

> 头文件只暴露命令获取接口，具体模块依赖放在 .cpp 中。

具体做法（后端 `CommandLibraryTagUI.cpp`）：接口在头文件，**依赖在 .cpp 里 include**。
前端对应：`InkingWindow.h` 只前向声明 `InkingScene`，`.cpp` 才 include 场景实现。

### 2.2 前向声明优先

**应该**用前向声明切断头文件之间的耦合，只在需要完整类型时（作为成员、调用方法）
才 include。后端的典型（`CommandExecutor.h`）：

```cpp
class CommandRegistrant;          // ← 前向声明，因为只用到指针

class CommandExecutor {
    CommandRegistrant *_commandRegistrant = nullptr;
};
```

**必须**成对遵守：**头文件里只做前向声明的类型，不该在头文件里调用它的方法**
（那要求完整类型）。实现搬到 `.cpp`。

### 2.3 include guard / pragma once

后端历史代码用 `#ifndef INKING_BACKEND_FRAMEWORK_<路径>_H`，前端新代码用 `#pragma once`。
**新文件统一用 `#pragma once`**，不再新增宏形式的 guard；改旧文件时顺手统一。

### 2.4 include 路径相对 `include/` 根

**必须**写成相对 include 根的路径，前缀与真实目录一致，**多写一段就找不到**：

```cpp
#include <ink/basic/InkingAnchor.h>    // 文件在 include/ink/basic/
#include <scene/SceneLibrary.h>         // 文件在 include/scene/
```

`include/scene/InkingScene.h` 曾经写成 `"ink/scene/SceneLibrary.h"`，
成了永远编译不过的死文件，而且它不在任何 target 的源文件列表里、没人 include，
**构建照样全绿，问题藏了很久**。

> 因此：**新增公开头之后，必须让它至少被一个 .cpp 或自检 include 一次。**
> 别拿"构建绿"当验证。

---

## 3. 类的写法

### 3.1 数据与逻辑分开

**必须**把纯数据抽成 `struct` 放 `dataStruct/`，不要塞进会干活的类里。
后端的 `Command` / `Stage` / `Message` / `Task` 都是纯数据 + 零成员函数。

判据：**这个类型有没有需要维护的不变量？** 有 → 类；没有 → `struct`。

### 3.2 单例

需要单例时（后端 `DatabaseManager`、前端 `TaskQueue`）**必须**：

```cpp
static DatabaseManager &instance() {
    static DatabaseManager inst;   // ← 函数内静态量
    return inst;
}
DatabaseManager(const DatabaseManager &) = delete;
DatabaseManager &operator=(const DatabaseManager &) = delete;
DatabaseManager(DatabaseManager &&) = delete;
DatabaseManager &operator=(DatabaseManager &&) = delete;   // ← 四个全禁
```

**必须**用**函数内静态量**，不用类的静态数据成员。
后者在 `main` 之前按翻译单元顺序初始化，静态对象可能先构造，
于是出现"场景活了库还没起来"甚至"对象在库已经析构之后才注销"——
静态初始化 / 析构顺序问题。**照抄后端的静态成员写法会带进这个坑，别抄。**

### 3.3 静态工具类

纯功能型（后端 `UIMessageLibrary`、前端 `MessageQueue`）用**静态类**：
全 `static` 方法 + 私有构造/析构：

```cpp
class UIMessageLibrary {
public:
    static void addMessage(...);
private:
    UIMessageLibrary();      // ← 私有，禁止实例化
    ~UIMessageLibrary();
    static std::mutex _mutex;
};
```

### 3.4 接口 + 实现分离

有"多种实现"预期的抽象**必须**抽成纯虚接口，头部只依赖接口。
后端 `IDatabase` 就是这个模式的范例：

```cpp
class IDatabase {                       // 纯接口，不知道 MySQL 存在
public:
    virtual QueryResult connect(...) = 0;
    virtual QueryResult execute(const std::string &sql) = 0;
    virtual ~IDatabase() = default;     // ← 虚析构必须有
};

class MySQLDatabase : public IDatabase { ... };   // 实现在子目录
```

`DatabaseManager.h` 的注释说明了意图：

> 其真正的工作是依靠 database 的具体实现，这里只提供调用方法的接口。

前端的对应：`SceneLibrary` 是接口层，具体场景由调用方派生——
**框架层不该认识任何具体业务类型。**

### 3.5 CMake 里的可选依赖

可选依赖**必须**做成编译期开关 + **能读懂的报错**，不能让人对着
"找不到 XXX"猜。后端 MySQL 那段的做法值得照抄：

```cmake
if(INKING_ENABLE_MYSQL)
    list(APPEND INKING_SOURCES src/database/MySQL/MySQLDatabase.cpp)
endif()
# ...找不到时：
message(FATAL_ERROR
    "MySQL client library not found.\n"
    "Recommended version: MySQL 8.4 LTS.\n"
    "If you do not need MySQL, configure with:\n"
    "  cmake -S . -B build -DINKING_ENABLE_MYSQL=OFF\n"
    "Or create cmake/CMakeUserPaths.cmake: ...")
```

要点：**说清怎么办**（推荐版本、关掉开关的命令、路径覆盖文件怎么写），
而不是只丢一句"找不到"。前端的 `cmake/SDL3.cmake` 已经照这个思路写了。

---

## 4. 解耦的四种常用手法

按优先级从高到低，**能用位置靠前的就别用靠后的**。

### 4.1 反向注册（谁想知道谁去查）

**不要**让底层认识上层。让上层把自己登记到一张表里，底层只认表、不认类型。

后端的实现：`ShineStatusChecker`（静态注册表）+ `StatusRegisterToken`（RAII 令牌）。
前端已经移植成 `SceneLibrary` + `RegisterToken`。用法：

```cpp
class MyModule : public ShineBasicModule {
    void registerToStatusChecker() {
        _statusRegisterToken.emplace(moduleName(), this);
    }
    std::optional<StatusRegisterToken> _statusRegisterToken;  // 延迟构造
};
```

**关键点**（前端的 `RegisterToken` 已经踩过并修正）：

- **令牌必须记住"我到底进没进库"**（`_active` 标志）。注册可能被驳回，
  驳回的令牌是空转的，析构时**绝不能**去注销——那会把正主从库里摘掉。
- **令牌用 `std::optional` 延迟构造**，不在初始化列表里。
- **令牌的成员声明顺序要在被登记的信息之后**，析构时 key 才全程有效。

### 4.2 依赖注入（传指针，不 new）

**应该**把协作者以指针/引用传进来，而不是自己 `new`、也不是直接调单例。
后端 `CommandExecutor` 就是：

```cpp
CommandExecutor(CommandRegistrant &commandRegistrant);   // 构造注入
CommandRegistrant *_commandRegistrant = nullptr;         // 非拥有关系
```

**必须**在命名和注释里写清**拥有关系**：`_commandRegistrant` 是"从那里取数据"，
不是它拥有的。前端对应：`InkingAnchor* _parent`、`InkingScene* _scene` 都标了"非拥有"。

### 4.3 门面（把 N 个子部件收成一个入口）

**应该**用一个瘦门面把多个协作部件收成单一入口，外部只认识门面。
后端 `CommandCenter` 是最好的例子：

```cpp
class CommandCenter : public ShineBasicModule {
private:
    CommandRegistrant _commandRegistrant;   // 数据与注册
    CommandExecutor   _commandExecutor;     // 实际执行
};
```

外部只调 `registerCommand` / `execute` / `printHelp`，
完全不知道内部拆成了"注册中心 + 执行器"两个东西。
**这让后来的重构不用动调用方**——这正是解耦的实际收益。

### 4.4 静态分发点（把"依赖"换成"登记"）

需要"某个模块提供的能力让别处用"时，**不要**让使用方 include 提供方。
后端 `CommandLibrary` 的做法：能力包成 `Command` 数据（含 `std::function`），
按类别拆成多个 `.cpp` 编译单元：

```text
CommandLibrary/CommandLibrary.cpp          ← 只知道有哪几类
CommandLibrary/CommandLibraryTagDatabase.cpp
CommandLibrary/CommandLibraryTagUI.cpp
```

`CommandLibrary.cpp` 全文就 10 行，只是把三类拼起来——
**新增一类命令不用动它以外的任何文件**。

---

## 5. 命名

**必须**统一，前后端一致：

| 位置 | 规则 | 例子 |
| --- | --- | --- |
| 类 / 结构体 | `PascalCase` | `CommandCenter`、`InkingScene` |
| 方法 / 函数 | `camelCase` | `registerCommand`、`GetSceneName` |
| 成员变量 | `_camelCase`（下划线开头） | `_mutex`、`_drawList`、`_conn` |
| 编译期常量 | `kPascalCase` | `kDesignWidth`、`kCommandCenterModuleName` |
| 枚举类 | `enum class`，值 `PascalCase` | `MessageType::Normal` |
| 头文件 | 与主类同名 | `CommandCenter.h` / `.cpp` |

**不要**引入 `m_` 前缀：后端（`_mutex`、`_commands`、`_conn`、`_threads`）和前端
（`_drawList`、`_sceneName`）都用裸下划线，两边已经一致，别再造第三套。

**必须**每个模块在头文件里暴露自己的模块名常量，供日志和自检用：

```cpp
inline constexpr const char *kDatabaseManagerModuleName = "DatabaseManager";
```

**前端补充约定**（本仓库已经成型，必须遵守）：
写入口 `PascalCase`（`SetVisible` / `Resize`），钩子 `on` 开头（`onTick` / `onDirty`）。
这条不是装饰——**`[final]` 写入口和 `[√]` 钩子在名字上就该能一眼分开**，
因为它们的契约完全不同：写入口内部统一标脏、不可重写；钩子只挂附加逻辑、可重写。

---

## 6. 注释与文档

**应该**用 `/** @brief ... */` 描述类，`/** 一句话 */` 描述成员，行尾对齐注释：

```cpp
void init(std::function<bool()> predicate, std::function<void()> execute);
std::array<std::unique_ptr<std::thread>, THREADNUM> _threads;  /** 数量钉死，过多线程反而降效率 */
```

**必须**在注释里写下**为什么**，而不只是**是什么**。后端的注释质量值得保持，
例如 `ThreadPool.h` 里解释为什么只唤醒一个管家线程：

> 以前这里是 notify_all，入队一次会把全部管家线程叫醒，其中若干个发现没活干再睡回去，
> 每次入队都要付出"全员唤醒 + 全员重新判断"的代价，任务一密集，CPU 就烧在调度上了。

这类"踩过坑才写得出"的注释是**必须保留的资产**。同理，本仓库
`AGENTS.md §6「已知坑」`和 `DevelopLog.md` 要继续维护——
前者记"以后别再踩"，后者记"当时为什么这么选"。

---

## 7. 错误处理

**必须**给"可能失败的边界"一个可表达的返回值，而不是靠抛异常穿过层边界。
后端 `JsonTool` 的做法：

```cpp
std::optional<Json> loadFromFile(const std::string &filePath) const;   // 读，失败返回 nullopt
bool saveToFile(...) const;                                            // 写，失败返回 false
```

`IDatabase` 的每个方法都返回 `QueryResult`，并且**把"连不上"和"已知损坏"分开**：

```cpp
virtual QueryResult ping() = 0;        // 主动探活，可能一次网络往返
virtual bool isBroken() const = 0;     // 只看本地状态，不产生往返
```

**应该**沿用这个判据：**能纯本地判断的就别做 I/O**，把代价不同的两件事拆成两个接口。

**必须**在长驻线程（管家线程、渲染循环）的入口兜住异常：

> 管家线程是长驻的，执行体抛出异常若一路逃逸出去，这条线程会被 std::terminate 干掉，
> 线程池会静默地少一个工人，之后所有任务的处理能力永久下降。

---

## 8. 并发

**必须**明确"哪些状态归哪条线程"，并在注释和文档里写死。本仓库的规矩：

| 子系统 | 线程归属 |
| --- | --- |
| 场景 / 渲染 / 输入 / 时间线 | **只在 UI 线程**（主循环那一条），因此**不加锁** |
| 任务队列 / 线程池 | 管家线程，跨线程 |
| 消息队列 | 生产者任意线程，消费者 UI 线程，**加锁** |

**必须**：跨线程的状态才加锁，并在头文件里说明"为什么加锁 / 为什么不加锁"。
前端 `SceneLibrary` 明确写了"有意不加锁，因为场景只在 UI 线程构造与析构"——
这条注释比锁本身更有价值，因为它标出了前提；前提变了（出现跨线程场景）就该加锁。

**必须**记住：**任务队列的回调跑在管家线程上**。任何碰 UI 状态的逻辑都不能放进去。
时间可以在别处算，**交付必须在 UI 线程**。

---

## 9. 提交前的自检

1. 新增文件、CMake、JSON 是否合法；
2. 完整构建绿（零警告），三个预设都过；
3. 跑自检 `ink_test.exe`，退出码 0；动了渲染再跑 `ink_render_probe.exe`；
4. **新增公开头**：确认它至少被一个 .cpp 或自检 include 过一次；
5. `AGENTS.md` / `API.md` / 开发日志与实现不一致的地方同步更新；
6. 不要在源码树里提交 `build/` 产物。

**验证方式的选择**（这条是踩过坑总结的）：

- **能变成数值判据的**（像素颜色、坐标、数量、顺序、计数）→ 自己写断言验，
  渲染类的用离屏读回（见 `tests/render_probe.cpp`）；
- **需要"看图"才能判断的**（字体、颜色观感、间距别扭不别扭、动画顺眼不顺眼）→
  **停下来交给用户看**，把示例跑起来、说明该看什么。别试图截图，会烧时间。

---

## 10. 一句话总结

> **分层单向、头文件只放声明、依赖用注入和反向注册而不是 include、
> 数据与逻辑分开、单例用函数内静态量、单体用门面收口、
> 注释写"为什么"、跨线程才加锁并写清前提。**
