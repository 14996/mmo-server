> **声明**：本仓库代码由 Claude Code 生成，仅供学习参考。

# MMO Server：精简版 MMORPG 服务端

一个 C++ 后端项目。在桌面 `src` 的 epoll Reactor 骨架上补齐网络层缺陷，并实现了一个可构建、可联调、可压测、可持久化的单进程多地图 MMORPG 服务端。

- **语言/依赖**：C++17，无第三方库（STL + 系统 socket）
- **网络模型**：主从 Reactor、非阻塞 TCP、环形读写缓冲、自研带版本号的二进制协议
- **可靠性**：会话状态机、心跳超时踢线（先存档）、单连接限流、非法包处理、发送队列上限
- **玩法**：3 张地图、34 只怪物、九宫格 AOI、战斗/技能/聊天/传送/升级/死亡复活
- **可运维**：外部配置文件、QPS/在线数/流量等运行指标、单元测试
- **配套工具**：headless 多线程机器人，可代替客户端做功能联调与压力测试

> 范围说明：本仓库是“游戏服”后端，不含登录服、网关、数据库和图形客户端。机器人只用于测试，不是正式客户端。

---

## 1. 快速开始

### Windows（MSYS2 / MinGW-w64）

```bash
./build_win.sh
./build_tests.sh         # 核心单元测试（缓冲/协议/限流/配置）
./run_demo.sh            # 一键启动服务器 + 20 个机器人，30 秒后自动退出
```

### Linux

```bash
./build.sh
./build_tests.sh
./run_demo.sh 20 30 9527
```

### 手动运行

```bash
# 终端 1：服务器，默认读取 config/server.conf
./build/mmo_server.exe --config config/server.conf

# 终端 2：20 个机器人，运行 30 秒
./build/bot_client.exe --host 127.0.0.1 --port 9527 --bots 20 --duration 30
```

Linux 下去掉 `.exe` 后缀。默认配置文件不存在时会回退到内置默认值；显式传入 `--config` 但文件缺失或配置非法时启动失败。命令行 `--ip` / `--port` 会覆盖配置文件。服务器支持 `--run-seconds N` 在 N 秒后自动优雅退出（0 表示常驻），便于脚本化测试。`Ctrl+C` 会触发保存并干净退出。

`run_demo.sh` 每次会清空 `data/`，启动带自动退出时间的服务器，跑完机器人后打印客户端指标、服务器日志尾部以及落盘角色数。

> **常见问题：端口被占用**。本机安装 VMware 时其 NAT 服务（vmnat.exe）会占用 9000 端口，所以默认端口已改为 9527。若启动时报 `Port already in use`，先用 `netstat -ano | findstr 9527` 查看占用，再换端口启动：
>
> ```bash
> ./build/mmo_server.exe --port 9528
> ```
>
> Windows 下监听 socket 使用独占绑定（`SO_EXCLUSIVEADDRUSE`），端口被占用会立即报错退出，不会与别的程序悄悄共用端口。

---

## 2. 功能清单

### 网络层

- 主从 Reactor：主线程 `accept`，连接按 fd 取模分配到最多 4 个 sub reactor
- 非阻塞 socket；Linux 使用 `epoll`，Windows 开发环境使用 `WSAPoll`
- 环形读写缓冲：读写游标分离，消费数据不整体搬移；收发均支持跨环形边界
- 16 字节固定包头 + 大端序包体，处理 TCP 粘包/半包/一次读多条
- 协议版本校验：版本不匹配、magic 错误、包体超限直接断连并记录日志
- 连接级发送缓冲：一次 `write` 写不完时保留剩余数据并注册可写事件，不丢包；积压超过 4 MB 断连
- 跨线程任务队列：`EventLoop::AddTask`，Linux 用 `eventfd` 唤醒，Windows 用短超时轮询
- 心跳超时：每秒扫描空闲会话，超时先保存世界数据再踢下线
- 单连接限流：固定窗口统计每秒消息数，超限断连
- 会话状态机：未登录 / 登录中 / 在线，未登录请求统一拒绝
- 运行指标：每 10 秒输出在线数、会话数、QPS、入站流量、踢线/非法包计数
- 连接关闭回调、延迟析构、启动/停止生命周期管理，退出前统一保存世界数据

### 游戏层

- 账号/角色：首次登录自动注册；密码哈希；重名、密码错误、重复在线校验；断线重连
- 地图：3 张地图、传送点、位置/移动冷却服务端校验
- AOI：8×8 格划分 + 九宫格视野；进入/离开视野与移动通知
- 战斗：服务端权威的普攻与两个技能；目标合法性、距离、MP、冷却校验
- 怪物 AI：索敌 → 追击 → 攻击 → 脱战回巢 → 回血 → 死亡后定时刷新
- 成长：击杀获得经验/金币，自动升级，属性随等级成长，最高 60 级
- 社交：地图内聊天广播、升级公告、战斗/死亡/复活通知
- 死亡与复活：玩家死亡 5 秒后原地复活；怪物 8 秒刷新
- 离线保留：断线后角色在地图保留一段时间（默认 60 秒，可配置），超时移出地图
- 持久化：默认 30 秒周期保存 + 登出保存 + 心跳踢线前保存 + 停机保存；临时文件重命名落盘

### 测试工具

- 多线程机器人：自动登录、心跳、移动、普攻、释放技能、聊天、切图、登出
- 支持指定机器人数量/运行时长/角色名前缀/起始编号
- 统计击杀、死亡、升级、切图、聊天、收发包数/字节数、伤害、连接失败等指标
- 一半机器人正常登出，一半直接断开，覆盖两条连接清理路径

### 工程化

- 外部配置：`config/server.conf`（监听地址、心跳超时、限流阈值、AOI 格大小、存档/离线时间）
- 核心单元测试：环形缓冲回绕/扩容、协议半包/粘包/版本/magic、限流窗口、配置解析
- `build_tests.sh` 一条命令构建并运行测试；CMake 下同时注册为 `ctest` 用例

---

## 3. 总体架构

```
                         ┌──────────────── main reactor ────────────────┐
TCP clients ────────────▶│ Acceptor ── 按 fd 取模 ──▶ 投递到 sub reactor │
                         └───────────────────────────────────────────────┘
                                          │ AddTask
             ┌────────────────────────────┼────────────────────────────┐
             ▼                            ▼                            ▼
      sub reactor 0                sub reactor 1                 sub reactor N
   Connection 读/写/发送        Connection 读/写/发送          Connection 读/写/发送
             └────────────────────────────┼────────────────────────────┘
                                          ▼
                         GameServer（全局 mutex 保护 World）
                                          ▲
                                          │ 每 50ms
                                    ticker 线程
```

### 3.1 线程模型

| 线程 | 职责 |
| --- | --- |
| 主线程 | 运行 main reactor，负责 `accept`、新连接投递、信号/退出协调 |
| sub reactor 线程 | 每个连接只属于一个 sub reactor，负责该连接的读、解码、业务回调和写 |
| ticker 线程 | 每 50ms 驱动 `World::Tick`：怪物 AI、刷新、回复、离线清理、定时存档；每秒扫描心跳超时，每 10 秒输出运行指标 |
| 机器人线程 | 每个机器人一条线程，阻塞 connect 后使用 `select` 多路复用 |

`GameServer::mutex_` 保护 `World`、`sessions_`、`player_conns_`。业务处理在锁内完成游戏状态变更，但**不在锁内做 socket I/O**：所有下行消息都编码后投递到连接所属 EventLoop，由该 reactor 线程实际发送。

### 3.2 连接生命周期

1. Reactor 读到 EOF/错误，`Connection::Close()` 先禁用事件，再调用业务关闭回调。
2. 业务回调在游戏锁内执行 `World::OnDisconnect` 并移除 `sessions_` / `player_conns_` 映射。
3. 随后 `Server` 把 `delete conn` 作为任务投递到连接所属 loop。
4. 发送任务与析构任务在同一 loop 的 FIFO 队列中执行，保证排队中的 `Send` 一定发生在 `delete` 之前。

心跳超时由 ticker 线程触发：它在**持有游戏锁**时把 `conn->Close()` 投递到连接所属 loop，再释放锁。这样关闭回调（需要同一把锁）无法抢先把 `delete` 任务排到队列前面，避免 ticker 持有悬空连接指针。这也是项目里重点处理过的 UAF（use-after-free）风险点。

---

## 4. 协议设计

### 4.1 包头（16 字节，网络字节序）

| 偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | magic | u32 | `0x4D4D4F31`，ASCII `"MMO1"` |
| 4 | msgId | u16 | 消息号 |
| 6 | version | u16 | 协议版本，当前为 `1`（复用原 flags 字段） |
| 8 | seq | u32 | 请求序号，响应原样带回 |
| 12 | bodyLen | u32 | 包体长度，上限 64 KB |

包体中的整数均为大端序；字符串编码为 `u16 长度 + 字节内容`。单连接未解析缓存上限为 256 KB。服务端只接受客户端消息 ID（1~8）；上报服务端下行消息 ID 会被丢弃并记录日志。版本不匹配、magic 错误或包体超限会直接断连。

### 4.2 消息表

| ID | 消息 | 方向 | 说明 |
| --- | --- | --- | --- |
| 1 | LoginReq | C→S | 角色名 + 密码 |
| 2 | HeartbeatReq | C→S | 心跳 |
| 3 | MoveReq | C→S | 方向 0/1/2/3 |
| 4 | ChatReq | C→S | 地图聊天 |
| 5 | AttackReq | C→S | 普攻目标 |
| 6 | SkillReq | C→S | 技能 ID + 目标 |
| 7 | ChangeMapReq | C→S | 请求切换地图 |
| 8 | LogoutReq | C→S | 主动登出 |
| 101 | LoginResp | S→C | 登录结果 / 错误信息 |
| 102 | HeartbeatResp | S→C | 心跳响应 |
| 103 | SceneInfo | S→C | 自身属性、地图信息、视野内实体快照 |
| 104 | EntityAppear | S→C | 实体进入视野 |
| 105 | EntityDisappear | S→C | 实体离开视野 |
| 106 | MoveNotify | S→C | 实体移动 |
| 107 | StatsNotify | S→C | 属性变化（升级、复活、回蓝等） |
| 108 | CombatNotify | S→C | 伤害、目标血量 |
| 109 | DeathNotify | S→C | 死亡与奖励 |
| 110 | RespawnNotify | S→C | 复活/刷新 |
| 111 | ChatNotify | S→C | 地图聊天广播 |
| 112 | ErrorNotify | S→C | 业务错误 |
| 113 | NoticeNotify | S→C | 系统公告 |
| 114 | LogoutResp | S→C | 登出响应 |

---

## 5. 关键实现

### 5.1 AOI 九宫格

每张地图按格边长（默认 8，可通过 `game.aoi_cell_size` 配置）切成网格，每个格子维护实体 ID 集合。实体只向“自己所在格 + 周围 8 格”内的玩家广播；移动时比较移动前后两个九宫格区域：

- 新进入区域：发送 `EntityAppear`
- 离开区域：发送 `EntityDisappear`
- 一直在区域内：只发送 `MoveNotify`
- 自己：同步收到自己的移动/属性通知

这样把全图广播降为局部广播，地图人数越多收益越明显。

### 5.2 服务端权威

客户端只发送“意图”（移动方向、攻击目标、技能 ID），不直接上报坐标或伤害。服务端校验：

- 移动方向合法、目标格在地图边界内、150ms 移动冷却
- 攻击目标存在且存活、同地图、在攻击距离内、800ms 普攻冷却
- 技能已配置、MP 足够、技能冷却结束、目标在技能射程内
- 传送点必须与玩家相邻

伤害公式为 `atk × 倍率 + 固定伤害 - def / 2`，再加 -2~+2 随机浮动。

### 5.3 粘包/半包

`Connection` 使用环形读缓冲；`GameServer` 把可读数据追加到每连接的 `parse_buffer`，循环调用 `DecodeFromBuffer`：

- 不足 16 字节包头或包体不完整：等待下一次读取
- magic 错误、协议版本不匹配或包体超限：关闭连接并记录日志
- 消息 ID 不是客户端允许的方向：丢弃该帧并记录日志，连接继续
- 解析成功：从缓冲区移除该帧并派发
- 单包最大 64 KB，单连接积压最大 256 KB，超限断连

### 5.4 心跳、限流与会话状态机

- 会话状态：`kUnauthenticated -> kAuthenticating -> kOnline`；只有登录请求能在未登录状态推进状态，其余业务请求返回 401。
- 心跳：客户端每 3 秒发送 `HeartbeatReq`；服务端每秒扫描 `last_active_ms`，超过 `net.heartbeat_timeout_ms`（默认 30 秒）先 `World::OnDisconnect` + `SaveAll()`，再关闭连接。
- 限流：每连接固定 1 秒窗口，允许 `net.max_packets_per_sec`（默认 300）条消息；超限直接断连。非法帧/非法消息 ID 单独计数。
- 限流只统计完整帧；半包洪水则受 256 KB 解析缓存上限约束。

### 5.5 配置与运行指标

`config/server.conf` 使用 `key=value` 格式，支持 `#` 注释；未知 key、非法数值都会在启动时报错。可配置项：

| key | 默认值 | 说明 |
| --- | --- | --- |
| `server.ip` / `server.port` | `0.0.0.0` / `9527` | 监听地址，命令行可覆盖 |
| `net.heartbeat_timeout_ms` | `30000` | 空闲踢线阈值 |
| `net.max_packets_per_sec` | `300` | 单连接每秒消息上限 |
| `game.save_interval_ms` | `30000` | 周期存档间隔 |
| `game.offline_retain_ms` | `60000` | 断线后角色保留时间 |
| `game.aoi_cell_size` | `8` | AOI 格边长 |

每 10 秒输出一行 `metrics:`，包含在线数、会话数、QPS、入站字节/秒、累计连接数、心跳踢线数、限流踢线数、非法包数，便于脚本采集；当前为同步日志，不是 Prometheus/异步日志。

### 5.6 相对原始 Reactor 骨架的修复

| 问题 | 修复 |
| --- | --- |
| 发送只调用一次 `write`，剩余数据被丢弃 | 增加发送缓冲与 `EPOLLOUT`/可写事件注册 |
| 读错误先置 `Closed`，导致 `Close()` 提前返回，业务关闭回调不触发 | 用独立 `close_notified_` 标记实现幂等关闭 |
| 数据帧后紧跟 FIN 时，最后一条消息被丢弃 | 先把已读数据交给业务回调，再执行关闭 |
| 新连接在 main reactor 线程操作 sub reactor 的 poller | 连接创建和 `EnableRead` 全部投递到所属 sub reactor |
| 连接删除与排队发送存在竞争 | 关闭回调先清映射，删除作为同 loop 任务延迟执行，FIFO 保证顺序 |

### 5.7 持久化

- 文件：`data/characters.dat`，每行一个角色：
  `id|name|password_hash|level|exp|hp|mp|gold|map|x|y`
- 保存时机：默认每 30 秒、主动登出、心跳超时踢线前、服务器停止
- 写入方式：先写 `characters.dat.tmp`，关闭文件后替换正式文件
- 启动时逐行解析并校验：非法角色名、重复 ID/重名、越界地图/坐标会被修正或跳过
- 密码使用 FNV-1a 哈希（**仅演示，不适用于生产**，生产应使用 Argon2/bcrypt 并配合 TLS）

---

## 6. 游戏数值

| 项 | 数值 |
| --- | --- |
| 地图 | 青石村 96×64、幽暗森林 128×80、熔岩矿洞 112×80 |
| 怪物 | 34 只：16 史莱姆、8 野狼 + 头狼、8 骷髅兵 + 矿洞守卫 |
| 玩家属性 | `maxHP=100+25×(lv-1)`，`maxMP=50+10×(lv-1)`，`atk=10+4×(lv-1)`，`def=2+2×(lv-1)` |
| 升级经验 | `100+80×(lv-1)`，最高 60 级 |
| 技能 1001 重击 | 10 MP，5 秒冷却，射程 1，`×2.0 + 5` |
| 技能 1002 火球术 | 15 MP，3 秒冷却，射程 6，`×1.5 + 3` |
| 节奏 | 移动 150ms、普攻 800ms、聊天 1s、回血回蓝 5s |
| 复活/刷新 | 玩家 5s、怪物 8s、离线保留 60s、存档 30s |

地图和怪物配置集中在 `src/GameConfig.cpp`，修改后重新编译即可。

---

## 7. 目录结构

```text
mmo-server/
├── src/
│   ├── include/
│   │   ├── Platform.h          # Linux/POSIX 与 Windows socket 差异封装
│   │   ├── EventLoop.h         # Reactor 事件循环 + 跨线程任务投递
│   │   ├── Poller.h            # epoll / WSAPoll 抽象
│   │   ├── Channel.h           # fd 事件注册
│   │   ├── Connection.h        # 连接读写、缓冲、生命周期
│   │   ├── Server.h            # 主从 Reactor、连接表
│   │   ├── Protocol.h          # 包头、版本、编解码、MsgId
│   │   ├── Messages.h          # 各业务消息结构
│   │   ├── RateLimiter.h       # 每连接固定窗口限流
│   │   ├── ServerConfig.h      # 运行时配置结构与加载接口
│   │   ├── GameConfig.h        # 地图/怪物/技能数值
│   │   ├── World.h             # 游戏世界与规则
│   │   └── GameServer.h        # 网络层与游戏层粘合、会话状态机
│   ├── EventLoop.cpp / Poller.cpp / Connection.cpp / Server.cpp / ...
│   ├── Protocol.cpp / Messages.cpp / ServerConfig.cpp
│   ├── GameConfig.cpp / World.cpp / GameServer.cpp
│   └── server_main.cpp         # 配置、信号、启动与优雅退出
├── tools/bot_client.cpp        # 多线程测试机器人
├── tests/test_core.cpp         # 核心单元测试
├── config/server.conf          # 运行配置
├── build.sh                    # Linux 构建
├── build_win.sh                # Windows/MSYS2 构建
├── build_tests.sh              # 构建并运行单元测试
├── CMakeLists.txt              # 可选 CMake 构建 + ctest
├── run_demo.sh                 # 一键联调/压测
└── data/characters.dat         # 运行时生成的角色存档
```

服务端代码约 5.3k 行，机器人与单元测试约 0.9k 行。

---

## 8. 构建说明

### Windows

要求 MSYS2 MinGW-w64 g++（C++17）。脚本默认使用 `g++`，也可通过环境变量指定：

```bash
CXX=g++ ./build_win.sh
```

Windows 构建加 `-static`，把 libstdc++/libgcc/winpthread 静态链接进 exe，避免因 PATH 中存在多套 MinGW 运行库而出现 `0xc0000139` 启动失败。

### Linux

```bash
sudo apt install build-essential
./build.sh
```

Linux 使用 epoll + eventfd；Windows 使用 WSAPoll，并在无 `eventfd` 的情况下用 10ms 轮询兼顾任务投递延迟。Windows 仅用于本地开发验证，Linux 是目标部署环境。

### CMake（可选）

```bash
cmake -S . -B build-cmake
cmake --build build-cmake -j
ctest --test-dir build-cmake --output-on-failure
```

产物为 `mmo_server`、`bot_client` 和 `test_core`。

### 运行参数

```text
mmo_server  [--config config/server.conf] [--ip 0.0.0.0] [--port 9527]
            [--run-seconds 0]
bot_client  [--host 127.0.0.1] [--port 9527] [--bots 20] [--duration 30]
            [--prefix bot] [--start-index 0]
```

---

## 9. 测试结果

测试环境：Windows 11 + MSYS2 UCRT64 g++ 14.2，本机回环，非正式 benchmark。

### 20 机器人 / 30 秒功能联调（加固后）

| 指标 | 结果 |
| --- | --- |
| 击杀 / 死亡 | 30 / 0 |
| 最高等级 | 1 |
| 切换地图 | 5 |
| 聊天消息 | 710 |
| 公告 | 5 |
| 收发包数 | 39,822 |
| 收发流量 | 1,248,852 字节 |
| 造成/承受伤害 | 1775 / 194 |
| 错误通知 | 42 |
| 丢包 / 连接失败 | 0 / 0 |
| 落盘角色数 | 20 |

### 100 机器人 / 30 秒冒烟压测（加固后）

| 指标 | 结果 |
| --- | --- |
| 收发包数 | 946,895（约 31k 包/秒） |
| 收发流量 | 29,286,629 字节 |
| 聊天消息 | 16,899 |
| 切换地图 | 25 |
| 击杀 | 35 |
| 错误通知 | 248 |
| 连接失败 | 0 |
| 服务端入站 QPS | 约 570（`metrics:` 日志） |
| 退出时在线连接 | 0 |
| 落盘角色数 | 100 |

### 加固项专项验证

| 场景 | 结果 |
| --- | --- |
| 单元测试 `build_tests.sh` | 缓冲回绕/扩容、协议半包/粘包/版本、限流窗口、配置解析全部通过 |
| 协议版本 = 2 | 服务端记录 `protocol version mismatch` 并断连 |
| 错误 magic | 服务端记录 `invalid frame header` 并断连 |
| 客户端上报消息 ID 101 | 该帧被丢弃并记录日志，随后心跳仍收到 102 响应 |
| 限流阈值 3 包/秒 | 3 个机器人约 1 秒内全部被断连，日志为 `rate limit exceeded` |
| 心跳阈值 1500ms + 静默客户端 | 约 1.8 秒后记录 `heartbeat timeout`，先保存再断连 |

说明：

- `错误通知` 是机器人攻击已被其他机器人击杀的目标等业务竞态产生的 `ErrorNotify`，不是协议解析错误或连接错误；`连接失败=0`，服务器无异常退出。
- 100 机器人测试中所有连接都被正确清理（在线数归零），100 个角色全部落盘。
- 另外验证过：传送跨图、怪物死亡刷新、玩家升级、断线保留与重新登录、停机后重启加载存档。
- 该压测只验证单机功能与连接稳定性，未做长时间稳定性、内存泄漏和公网带宽测试。

---

## 10. 已知限制与可扩展方向

### 当前限制

- **全局一把游戏锁**：逻辑简单、无数据竞争，但多核扩展性有限；广播和 AI 都会串行化。
- **业务仍在 IO 线程执行**：解码后直接在同一 sub reactor 线程处理业务，没有按玩家串行队列 + 业务线程池拆分；重逻辑会阻塞该 reactor。
- **单进程**：没有网关、分线、跨服、分布式会话，也没有 trace_id 跨服务追踪。
- **文件存档**：全量写文件，不支持事务、回滚、分库分表；没有 MySQL 连接池、Redis 缓存和脏标记批量落库，适合演示，不适合真实运营。
- **无碰撞/寻路**：格子直走，没有障碍物、A* 和视线判断。
- **无图形客户端**：仓库内只有 headless 机器人；另有一个 Python/tkinter 演示客户端（独立目录，不随本仓库发布），不包含美术资源、预测/插值等正式客户端能力。
- **安全能力有限**：密码哈希为演示级，未接 TLS、验证码、风控和审计；已实现单连接包速率限制与非法包断连，但没有账号级/IP 级风控。
- **可观测性有限**：有同步分级日志和 10 秒运行指标，但没有异步日志、崩溃转储/core stack、Prometheus 指标和链路追踪。
- **Linux epoll 路径与 CMake 脚本**：代码路径已保留，但本机开发环境为 Windows，需在 Linux 上做一次编译和回归。

### 可扩展方向

- 按玩家/地图把业务投递到有界线程池，并保证同一玩家消息串行处理
- 按地图/场景分片，每个场景一个逻辑线程，减少全局锁竞争
- 引入 MySQL 连接池 + Redis：角色、背包、排行榜、会话；存档改为脏标记异步批量落库
- AOI 增量维护：跨格时只更新变化的边界，支持更复杂的视野形状
- 协议升级：Protobuf/FlatBuffers、压缩、加密
- 战斗系统：Buff/Debuff、仇恨列表、掉落表、副本与队伍
- 工程化：配置热更新、Prometheus 指标、异步/结构化日志、崩溃转储、ASan/TSan 回归
- 客户端：制作 SDL/Raylib/WebSocket 正式图形客户端，增加移动预测与插值
