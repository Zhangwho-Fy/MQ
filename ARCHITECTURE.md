# MQ 轻量级消息中间件 —— 架构与技术文档

## 1. 项目概述

MQ 是一个基于 C++17 实现的轻量级消息队列中间件，兼容 AMQP 0-9-1 核心协议语义。支持多交换机类型（直连、扇形、主题）、消息持久化、消费者确认（ACK/NACK）、死信队列等特性。项目采用 Client-Server 架构，通过 TCP 长连接进行通信，使用 Protobuf 序列化协议，适用于微服务间异步解耦、任务分发、日志收集等场景。

---

## 2. 技术栈与选型理由

| 技术 | 用途 | 选型理由 |
|------|------|----------|
| **C++17** | 开发语言 | 高性能、零成本抽象；`std::optional`/`std::variant`/折叠表达式等特性简化代码；RAII 管理资源生命周期 |
| **muduo** | 异步网络库 | 基于 Reactor 模式（one-loop-per-thread），事件驱动非阻塞 I/O；封装 epoll，提供 `TcpServer`/`TcpClient`/`EventLoop` 等高度抽象；成熟稳定，陈硕经典开源项目 |
| **Protobuf** | 序列化协议 | 跨语言、二进制紧凑编码、强类型 Schema、向前/向后兼容；相比 JSON 体积更小、解析更快；相比 Thrift 更轻量、生态更好 |
| **SQLite3** | 元数据持久化 | 嵌入式、零配置、无服务端进程；单文件存储适合嵌入式中间件场景；支持事务保证一致性；无需额外运维成本 |
| **文件存储** | 消息体持久化 | 顺序写性能优于数据库 BLOB；自定义格式（长度前缀 + payload）便于垃圾回收；每个队列独立文件隔离故障影响面 |

### 为什么不用现成方案

- **RabbitMQ**：依赖 Erlang 运行时，部署重，不适合嵌入或轻量场景
- **Kafka**：依赖 ZooKeeper，侧重流式日志，对低延迟单条消息场景过重
- **ZeroMQ**：无 Broker，缺少持久化和路由能力，更像底层通信库

本项目定位是"可嵌入 C++ 应用的轻量 Broker"，提供 AMQP 核心语义，部署只需一个二进制文件。

---

## 3. 总体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Client (publisher)                       │
│  publish_client.cc                                               │
│  AsyncWorker ──▶ Connection ──▶ Channel                          │
│                      │              │                            │
│                      │    TCP (Protobuf + muduo codec)           │
│                      │              │                            │
│  consume_client.cc   │              │                            │
│  AsyncWorker ──▶ Connection ──▶ Channel                          │
│                      │                                         │
└──────────────────────┼─────────────────────────────────────────┘
                       │
┌──────────────────────┼─────────────────────────────────────────┐
│                      ▼            Server (Broker)                │
│  mq_server.cc                                                    │
│  Server (mq_broker.hpp)                                          │
│  ├── EventLoop (主 Reactor)                                      │
│  ├── TcpServer (监听 :8085)                                      │
│  ├── ProtobufDispatcher (请求路由)                               │
│  ├── ConnectionManager ──▶ Connection ──▶ ChannelManager         │
│  │                                           └── Channel         │
│  ├── VirtualHost (核心调度层)                                    │
│  │   ├── ExchangeManager  (交换机管理 + SQLite 持久化)           │
│  │   ├── MsgQueueManager  (队列管理 + SQLite 持久化)             │
│  │   ├── BindingManager   (绑定管理 + SQLite 持久化)             │
│  │   └── MessageManager   (消息持久化 + 文件存储)                │
│  │       └── QueueMessage (per-queue: 待推送/持久/待确认链表)    │
│  ├── ConsumerManager (消费者管理 + 轮转调度)                     │
│  │   └── QueueConsumer (per-queue: 消费者列表 + Round-Robin)     │
│  └── threadpool (消费任务异步执行)                                │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. 模块详解

### 4.1 mqcommon —— 公共基础模块

#### 4.1.1 日志系统 (`mq_logger.hpp`)

```cpp
#define DLOG(format,...)   // DEBUG 级别，黄色
#define ILOG(format,...)   // INFO 级别，绿色
#define ELOG(format,...)   // ERROR 级别，红色
```

带时间戳、文件名、行号输出，通过 `DEFAULT_LEVEL` 宏控制编译期日志级别。

#### 4.1.2 UUID 生成 (`mq_helper.hpp` — `UUIDHelper`)

`uuid()` 基于 `std::mt19937_64` 伪随机数 + 原子递增序列号混合生成，格式为 `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`，保证全局唯一性，用作消息 ID、请求 ID、信道 ID。

#### 4.1.3 SQLite 辅助类 (`mq_helper.hpp` — `SqliteHelper`)

封装 `sqlite3_open_v2` / `sqlite3_exec` / `sqlite3_close_v2`，支持 `SQLITE_OPEN_FULLMUTEX` 多线程安全模式。

#### 4.1.4 文件辅助类 (`mq_helper.hpp` — `FileHelper`)

二进制读写、存在性判断、目录创建与删除、文件重命名，覆盖消息持久化的 I/O 需求。

#### 4.1.5 线程池 (`mq_threadpool.hpp`)

- 基于 `std::packaged_task` + `std::future` 实现异步任务提交与结果获取
- 采用生产者-消费者模型，`push()` 提交任务，worker 线程通过 `condition_variable` 等待、批量取任务执行
- 返回值通过 `std::future` 异步获取

#### 4.1.6 Protobuf 协议定义

**消息模型** (`mq_msg.proto`)：

```protobuf
enum ExchangeType { DIRECT = 1; FANOUT = 2; TOPIC = 3; }
enum DeliverMode { UNDURABLE = 1; DURABLE = 2; }

message BasicProperties {
    string id = 1;              // 消息唯一ID
    DeliverMode deliver_mode = 2; // 投递模式
    string routing_key = 3;     // 路由键
}

message Message {
    Payload payload = 1;
    uint32 offset = 2;   // 在持久化文件中的偏移
    uint32 length = 3;   // payload 序列化长度
}
```

**通信协议** (`mq_proto.proto`)：定义了完整的请求-响应模型，包括信道管理（open/close channel）、交换机声明/删除、队列声明/删除/绑定/解绑、消息发布、消息消费（basicConsume/basicCancel）、消息确认（basicAck），以及服务端推送响应（basicConsumeResponse）和通用响应（basicCommonResponse）。

---

### 4.2 mqclient —— 客户端 SDK

客户端通过四层抽象实现消息的发布与消费：

```
AsyncWorker ──▶ Connection ──▶ Channel ──▶ 具体操作 (publish / consume / declare)
```

#### 4.2.1 AsyncWorker (`mq_worker.hpp`)

```cpp
class AsyncWorker {
    muduo::net::EventLoopThread loopthread;  // 独立事件循环线程
    threadpool pool;                          // 消息处理线程池
};
```

- `loopthread`：提供独立的 EventLoop 线程，驱动 muduo TcpClient 的 I/O 事件
- `pool`：消费回调在线程池中执行，避免阻塞网络 I/O

#### 4.2.2 Connection (`mq_connection.hpp`)

```cpp
Connection(ip, port, worker)
  ├── muduo::net::TcpClient    // TCP 连接
  ├── ProtobufDispatcher       // 按消息类型分发回调
  ├── ProtobufCodec            // Protobuf 编解码
  ├── CountDownLatch           // 同步等待连接建立
  └── ChannelManager           // 管理该连接下的所有信道
```

- `openChannel()` 创建信道并发送 `openChannelRequest` 到服务端
- 注册 `basicCommonResponse` 回调（收到响应后通过 CID 路由到对应 Channel，唤醒等待线程）
- 注册 `basicConsumeResponse` 回调（收到服务端推送后通过 CID 找到 Channel，投递到线程池执行消费者回调）

#### 4.2.3 Channel (`mq_channel.hpp`)

客户端 Channel 是操作入口，提供全部 AMQP 语义：

```cpp
// 资源声明
bool declareExchange(name, type, durable, auto_delete, args);
void deleteExchange(name);
bool declareQueue(qname, durable, exclusive, auto_delete, args);
void deleteQueue(qname);
bool QueueBind(ename, qname, key);
void QueueUnBind(ename, qname);

// 消息操作
void basicPublish(ename, BasicProperties*, body);    // 发布消息
bool basicConsumer(tag, qname, auto_ack, callback);   // 订阅队列
void basicAck(msgid);                                 // 确认消费
void basicCancel();                                   // 取消订阅
```

**同步等待机制**：每个请求携带一个 `rid`（request ID），`waitResponse(rid)` 通过 `condition_variable` 阻塞等待，直到收到带相同 `rid` 的 `basicCommonResponse`。所有请求都是同步阻塞的（发送请求 → 等待响应）。

#### 4.2.4 Consumer (`mq_consumer.hpp`)

```cpp
struct Consumer {
    std::string tag;               // 消费者标签
    std::string qname;             // 订阅的队列
    bool auto_ack;                 // 自动确认
    ConsumerCallback callback;     // 消息处理回调
};
```

回调签名：`void(const string &consumer_tag, const BasicProperties *bp, const string &body)`

**发布客户端示例** (`publish_client.cc`)：
1. 创建 AsyncWorker → Connection → Channel
2. 声明 TOPIC 交换机 `exchange1`
3. 声明两个队列 `queue1`、`queue2`
4. 绑定：`queue1 ← exchange1 (key=queue1)`，`queue2 ← exchange1 (key=news.music.#)`
5. 循环发布消息（routing_key = `news.music.pop` / `news.music.sport` / `news.sport`）

**消费客户端示例** (`consume_client.cc`)：
1. 同上步骤 1-4
2. `basicConsumer` 订阅队列，注册回调
3. 收到消息后在回调中打印并调用 `basicAck` 确认

---

### 4.3 mqserver —— 服务端 Broker

服务端是消息中间件的核心，负责连接管理、消息路由、持久化和消费者调度。

#### 4.3.1 Server / Broker (`mq_broker.hpp`)

```cpp
Server(port, basedir)
  ├── muduo::net::EventLoop     // 主事件循环
  ├── muduo::net::TcpServer     // 监听端口
  ├── ProtobufDispatcher        // 按消息类型分发到业务处理函数
  ├── ProtobufCodec             // 编解码
  ├── VirtualHost               // 核心调度层
  ├── ConsumerManager           // 消费者管理
  ├── ConnectionManager         // 连接管理
  └── threadpool                // 消费任务线程池
```

`start()` 调用 `_server.start()` 启动监听，然后 `_baseloop.loop()` 进入事件循环。

**请求分发流程**：
1. TcpServer 收到数据 → ProtobufCodec 反序列化
2. ProtobufDispatcher 根据消息类型分发到对应 handler（如 `onBasicPublish`）
3. handler 通过 `runOnChannel()` 模板方法：查 Connection → 查 Channel → 执行业务函数

注册的 12 个业务处理函数：

```
openChannelRequest    → onOpenChannel
closeChannelRequest   → onCloseChannel
declareExchangeRequest → onDeclareExchange
deleteExchangeRequest → onDeleteExchange
declareQueueRequest   → onDeclareQueue
deleteQueueRequest    → onDeleteQueue
queueBindRequest      → onQueueBind
queueUnBindRequest    → onQueueUnBind
basicPublishRequest   → onBasicPublish
basicAckRequest       → onBasicAck
basicConsumeRequest   → onBasicConsume
basicCancelRequest    → onBasicCancel
```

#### 4.3.2 VirtualHost (`mq_host.hpp`)

VirtualHost 是消息调度的核心枢纽，聚合四大管理器：

```
VirtualHost
  ├── ExchangeManager  (_emp)
  ├── MsgQueueManager  (_mqmp)
  ├── BindingManager   (_bmp)
  └── MessageManager   (_mmp)
```

对外提供统一的 CRUD 接口，内部协调多个管理器的联动：
- 声明交换机时：ExchangeManager 新建（durable → SQLite 插入）
- 声明队列时：MessageManager 初始化存储 + MsgQueueManager 新建
- 删除交换机/队列时：联动删除关联的绑定和消息
- `basicPublish`：路由判断 → 消息入队 → 异步消费
- `basicConsume`：从队列取消息（`front()` 移入待确认列表）
- `basicAck`：确认删除消息（持久化消息物理删除）

#### 4.3.3 Exchange 交换机 (`mq_exchange.hpp`)

三层设计：

```
Exchange (数据结构)
  ├── name / type / durable / auto_delete / args

ExchangeMapper (SQLite 持久化)
  ├── createTable / insert / remove / recovery

ExchangeManager (内存管理 + 线程安全)
  ├── declareExchange / deleteExchange / selectExchange / exists
  └── 内部维护 unordered_map<string, Exchange::ptr>
```

- `DIRECT`：精确匹配 routing_key == binding_key
- `FANOUT`：广播到所有绑定队列
- `TOPIC`：支持 `*`（匹配单层）和 `#`（匹配零或多层）通配符

#### 4.3.4 Queue 队列 (`mq_queue.hpp`)

与 Exchange 对称的三层设计：
- `MsgQueue`：数据结构（name, durable, exclusive, auto_delete, args）
- `MsgQueueMapper`：SQLite 持久化
- `MsgQueueManager`：内存管理 + 互斥锁

#### 4.3.5 Binding 绑定 (`mq_binding.hpp`)

```cpp
BindingMap = unordered_map<exchange_name,
              unordered_map<queue_name, Binding::ptr>>
```

- 绑定持久化到 SQLite（联合主键 `exchange_name + msgqueue_name`）
- 级联删除：删除交换机时移除所有绑定，删除队列时移除所有绑定

#### 4.3.6 Message 消息持久化 (`mq_message.hpp`)

是整个持久化系统最复杂的模块，三部分：

**MessageMapper**：文件级操作
- 存储格式：`[4字节 payload长度][payload序列化数据]`
- 删除采用**逻辑删除 + 垃圾回收**策略：标记 `valid="0"` 而非直接物理删除
- `gc()`：读取所有有效数据 → 写入临时文件 → 删除原文件 → 重命名临时文件

**QueueMessage**：per-queue 消息状态管理
- `_msgs`：待推送消息链表（`std::list`）
- `_durable_msgs`：持久化消息索引（`unordered_map<id, MessagePtr>`）
- `_waitack_msgs`：待确认消息索引（已推送但未 ACK）

推送生命周期：
```
insert → _msgs     (生产者发布)
front  → _waitack_msgs  (消费者拉取)
remove → 物理删除   (消费者 ACK)
```

**GC 策略**：当持久化消息总量 > 2000 且有效比例 < 50% 时触发垃圾回收，防止文件无限膨胀。

**MessageManager**：管理所有队列的 `QueueMessage` 实例，线程安全。

#### 4.3.7 Router 路由引擎 (`mq_route.hpp`)

路由键与绑定键合法性校验：
- routing_key 合法字符：`a-z A-Z 0-9 . _`
- binding_key 额外支持 `*` 和 `#`，但 `*`/`#` 必须独立为一个单词段

三种交换类型的匹配算法：
- **DIRECT**：`routing_key == binding_key`
- **FANOUT**：始终返回 `true`
- **TOPIC**：动态规划匹配，`*` 匹配恰好一个单词，`#` 匹配零个或多个单词

#### 4.3.8 Consumer 消费者管理 (`mq_consumer.hpp`)

```
ConsumerManager (全局)
  └── QueueConsumer × N (per-queue)
        └── Consumer × M (消费者实例列表)
```

- `QueueConsumer::choose()` 使用 **Round-Robin** 轮转调度选择消费者，保证同一队列的多个消费者公平分配消息
- Channel 析构时自动注销消费者（RAII）

#### 4.3.9 服务端 Connection (`mq_connection.hpp`)

与客户端 Connection 对称：
- `Connection`：持有一个 `ChannelManager`，处理 open/close channel 请求
- `ConnectionManager`：将所有在线连接索引在 `unordered_map<TcpConnectionPtr, Connection::ptr>` 中
- 新连接到达时创建 Connection 对象，断开时自动清理

#### 4.3.10 服务端 Channel (`mq_channel.hpp`)

服务端 Channel 是业务处理的实际执行者：

```cpp
Channel(id, host, cmp, codec, conn, pool)
```

- 收到 `declareExchange` 请求 → 委托给 `VirtualHost::declareExchange()` → 返回 basicCommonResponse
- 收到 `basicPublish` 请求 → 路由匹配 → 消息入队 → 投递消费任务到线程池
- 收到 `basicConsume` 请求 → 注册消费者（`ConsumerManager::create()`）→ 后续消息推送通过 `callback()` → `ProtobufCodec::send()` 推送到客户端
- 收到 `basicAck` 请求 → `VirtualHost::basicAck()` → 消息物理删除

---

## 5. 核心流程时序

### 5.1 消息发布流程

```
Publisher                    Server                      Storage
   │                           │                            │
   │  basicPublish(exchange,   │                            │
   │    properties, body)      │                            │
   │ ─────────────────────────▶│                            │
   │                           │ 1. selectExchange()        │
   │                           │ 2. getExchangeBindings()   │
   │                           │ 3. Router::route() 匹配    │
   │                           │ 4. basicPublish()          │
   │                           │ ──────────────────────────▶│
   │                           │          insert            │
   │                           │    (持久化: DURABLE消息    │
   │                           │     写入 .mqb 文件)        │
   │                           │ 5. 投递 consume 任务到     │
   │                           │    threadpool              │
   │  basicCommonResponse(ok)  │                            │
   │ ◀─────────────────────────│                            │
```

### 5.2 消息消费流程

```
Consumer                    Server                      Storage
   │                           │                            │
   │  basicConsume(tag,        │                            │
   │    queue, auto_ack, cb)   │                            │
   │ ─────────────────────────▶│                            │
   │                           │ ConsumerManager::create()  │
   │  basicCommonResponse(ok)  │                            │
   │ ◀─────────────────────────│                            │
   │                           │                            │
   │      [异步推送，threadpool 中执行]                      │
   │                           │ 1. basicConsume(qname)     │
   │                           │ ──────────────────────────▶│
   │                           │    front() → _waitack_msgs │
   │                           │ 2. QueueConsumer::choose() │
   │                           │    (Round-Robin)           │
   │                           │ 3. consumer->callback()    │
   │  basicConsumeResponse     │                            │
   │ ◀─────────────────────────│                            │
   │                           │                            │
   │  basicAck(msgid)          │                            │
   │ ─────────────────────────▶│                            │
   │                           │ _host->basicAck()          │
   │                           │ ──────────────────────────▶│
   │                           │   remove(msg_id)           │
   │                           │   (持久化: 标记valid='0'   │
   │                           │    触发 GC 条件时回收)     │
   │  basicCommonResponse(ok)  │                            │
   │ ◀─────────────────────────│                            │
```

### 5.3 Topic 路由匹配示例

```
交换机类型: TOPIC
绑定: queue1 ← exchange1 (key = "news.music.#")
      queue2 ← exchange1 (key = "news.music.*")

routing_key = "news.music.pop"
  → 匹配 "news.music.#"  ✓ → 投递到 queue1
  → 匹配 "news.music.*"  ✓ → 投递到 queue2

routing_key = "news.music.sport.jazz"
  → 匹配 "news.music.#"  ✓ → 投递到 queue1
  → 匹配 "news.music.*"  ✗

routing_key = "news.sport"
  → 两个都不匹配

routing_key = "queue1"
  → DIRECT 交换机才会匹配（精确相等）
```

---

## 6. 持久化设计

### 6.1 元数据持久化（SQLite3）

`./data/meta.db` 中包含三张表：

```sql
-- 交换机表
exchange_table(name, type, durable, auto_delete, args)

-- 队列表
queue_table(name, durable, exclusive, auto_delete, args)

-- 绑定表（联合主键）
binding_table(exchange_name, msgqueue_name, binding_key)
```

启动时通过 `recovery()` 从 SQLite 恢复全部元数据到内存。

### 6.2 消息体持久化（文件存储）

每个持久化队列对应一个 `.mqb` 文件：`./data/{queue_name}.mqb`

```
文件格式:
┌──────────────┬──────────────────────┐
│ 4字节: size  │ 序列化的 Payload     │
├──────────────┼──────────────────────┤
│ 4字节: size  │ 序列化的 Payload     │
├──────────────┼──────────────────────┤
│ ...          │ ...                  │
└──────────────┴──────────────────────┘
```

- **逻辑删除**：标记 `valid = "0"`，不移动数据
- **垃圾回收**：`total > 2000 && valid * 2 < total` 时触发，将有效数据 compact 到新文件
- 恢复时使用 `gc()` 加载所有有效消息

### 6.3 恢复流程

1. Server 启动 → VirtualHost 构造
2. ExchangeManager / MsgQueueManager / BindingManager 从 SQLite 恢复
3. MessageManager 遍历所有队列 → 恢复 `.mqb` 文件中的有效消息
4. ConsumerManager 初始化每个队列的消费者管理结构

---

## 7. 并发模型

```
┌──────────────────────────────────────────────┐
│  主线程 (EventLoop)                           │
│  - muduo I/O 事件循环                         │
│  - 接收 TCP 连接和数据                        │
│  - Protobuf 反序列化                          │
│  - 请求分发（同步，不阻塞）                   │
│  - 基本响应发送                               │
└──────────────┬───────────────────────────────┘
               │ 投递消费任务
               ▼
┌──────────────────────────────────────────────┐
│  threadpool (工作线程池)                      │
│  - 异步执行消费推送                           │
│  - 路由匹配                                   │
│  - 消息推送 (basicConsumeResponse → 客户端)   │
└──────────────────────────────────────────────┘
```

- 各 Manager 内部使用 `std::mutex` + `std::unique_lock` 保护共享数据
- Channel 使用 `condition_variable` 实现请求-响应的同步阻塞等待
- 消费回调在独立线程池中执行，避免阻塞网络 I/O

---

## 8. API 接口汇总

### 8.1 客户端 Channel API

```cpp
// 信道管理
bool openChannel();
void closeChannel();

// 交换机管理
bool declareExchange(name, ExchangeType, durable, auto_delete, args);
void deleteExchange(name);

// 队列管理
bool declareQueue(qname, durable, exclusive, auto_delete, args);
void deleteQueue(qname);

// 绑定管理
bool QueueBind(ename, qname, binding_key);
void QueueUnBind(ename, qname);

// 消息操作
void basicPublish(ename, BasicProperties*, body);
bool basicConsumer(tag, qname, auto_ack, callback);
void basicAck(message_id);
void basicCancel();
```

### 8.2 通信协议（Protobuf，共 12 种消息类型）

| 方向 | 消息类型 | 说明 |
|------|---------|------|
| C→S | `openChannelRequest` | 创建信道 |
| C→S | `closeChannelRequest` | 关闭信道 |
| C→S | `declareExchangeRequest` | 声明交换机 |
| C→S | `deleteExchangeRequest` | 删除交换机 |
| C→S | `declareQueueRequest` | 声明队列 |
| C→S | `deleteQueueRequest` | 删除队列 |
| C→S | `queueBindRequest` | 绑定队列到交换机 |
| C→S | `queueUnBindRequest` | 解绑 |
| C→S | `basicPublishRequest` | 发布消息 |
| C→S | `basicAckRequest` | 确认消费 |
| C→S | `basicConsumeRequest` | 订阅队列 |
| C→S | `basicCancelRequest` | 取消订阅 |
| S→C | `basicCommonResponse` | 通用响应（rid, cid, ok） |
| S→C | `basicConsumeResponse` | 推送消息给消费者 |

---

## 9. 项目结构

```
MQ/
├── README.md
├── ARCHITECTURE.md              ← 本文档
├── mqcommon/                    # 公共基础库
│   ├── mq_logger.hpp            # 日志宏（DBG/INF/ERR）
│   ├── mq_helper.hpp            # SQLite/文件/UUID/字符串工具
│   ├── mq_threadpool.hpp        # 通用线程池
│   ├── mq_msg.proto             # 消息模型定义
│   └── mq_proto.proto           # 通信协议定义
├── mqclient/                    # 客户端 SDK
│   ├── mq_worker.hpp            # AsyncWorker（EventLoop线程 + 线程池）
│   ├── mq_connection.hpp        # 客户端连接管理
│   ├── mq_channel.hpp           # 客户端信道（同步 API）
│   ├── mq_consumer.hpp          # 消费者结构体
│   ├── publish_client.cc        # 发布客户端示例
│   └── consume_client.cc        # 消费客户端示例
├── mqserver/                    # 服务端 Broker
│   ├── mq_server.cc             # 入口
│   ├── mq_broker.hpp            # Server 类（监听 + 事件循环 + 请求分发）
│   ├── mq_connection.hpp        # 服务端连接管理
│   ├── mq_channel.hpp           # 服务端信道（业务处理）
│   ├── mq_host.hpp              # VirtualHost（核心调度枢纽）
│   ├── mq_exchange.hpp          # 交换机定义 + SQLite + 内存管理
│   ├── mq_queue.hpp             # 队列定义 + SQLite + 内存管理
│   ├── mq_binding.hpp           # 绑定关系 + SQLite + 内存管理
│   ├── mq_message.hpp           # 消息持久化 + 文件存储 + GC
│   ├── mq_consumer.hpp          # 消费者管理 + Round-Robin 调度
│   ├── mq_route.hpp             # 路由引擎（DIRECT/FANOUT/TOPIC）
│   └── data/                    # 运行时数据目录
│       └── meta.db              # SQLite 元数据库
└── mqdemo/                      # 学习示例（future/gtest/log/muduo）
```

---

## 10. 设计约束与未来方向

### 当前设计约束
- 每个 Channel 同时只能消费一个队列（`_consumer` 单例）
- 客户端请求均为同步阻塞（发送请求后阻塞等待响应）
- 单个 VirtualHost（不支持多 vhost）
- 缺少用户认证与权限控制
- 服务端不支持集群/主从复制

### 可扩展方向
- **多 VirtualHost**：支持租户隔离
- **异步客户端 API**：返回 `std::future` 而非阻塞
- **集群模式**：Raft 共识 + 主从同步
- **管理接口**：HTTP API 用于运维监控
- **死信队列**：消息 TTL + 重试次数超限后转入 DLQ
- **流量控制**：QoS prefetch count，背压机制
