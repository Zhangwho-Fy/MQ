# MQ：轻量级消息中间件

一个基于 C++17 实现的轻量级消息队列，兼容 AMQP 核心协议语义，支持多交换机类型、消息持久化、消费者确认等功能。

---

## ✨ 核心特性
- **高性能**：基于 muduo Reactor 异步网络库，支持高并发连接与消息分发
- **多交换机类型**：直连（DIRECT）、扇形（FANOUT）、主题（TOPIC），支持路由键与通配符匹配
- **持久化支持**：元数据写入 SQLite3，消息体二进制文件存储，重启不丢失
- **可靠投递**：消费者确认（ACK） + 垃圾回收（GC），保证消息不丢失
- **客户端 SDK**：提供 C++ 客户端，连接 / 信道 / 消费者三层抽象

---

## 🛠️ 技术栈

| 技术 | 用途 | 选型理由 |
|------|------|----------|
| C++17 | 开发语言 | 高性能、RAII 资源管理 |
| [muduo](https://github.com/chenshuo/muduo) | 异步网络库 | Reactor 模式，事件驱动非阻塞 I/O |
| Protobuf | 序列化协议 | 二进制紧凑编码，强类型 Schema |
| SQLite3 | 元数据持久化 | 嵌入式、零配置、单文件存储 |
| 文件存储 | 消息体持久化 | 顺序写性能优，per-queue 隔离 |

---

## 🏗️ 架构概览

```
Client (publisher / consumer)                  Server (Broker)
┌───────────────────────────┐     TCP      ┌──────────────────────────────┐
│ AsyncWorker               │  (Protobuf)  │ Server                       │
│  ├── EventLoopThread      │◄───────────▶│  ├── EventLoop (主 Reactor)   │
│  └── threadpool           │              │  ├── TcpServer (监听)         │
│                           │              │  ├── ProtobufDispatcher      │
│ Connection                │              │  ├── ConnectionManager        │
│  ├── TcpClient            │              │  │   └── Connection × N      │
│  ├── ProtobufDispatcher   │              │  │       └── Channel × K     │
│  └── ChannelManager       │              │  ├── VirtualHost (调度核心)   │
│      └── Channel × N      │              │  │   ├── ExchangeManager     │
│          ├── declare*     │              │  │   ├── MsgQueueManager     │
│          ├── publish      │              │  │   ├── BindingManager      │
│          └── consume      │              │  │   └── MessageManager      │
└───────────────────────────┘              │  ├── ConsumerManager         │
                                           │  │   └── QueueConsumer × Q   │
                                           │  │       └── Consumer × R    │
                                           │  └── threadpool              │
                                           └──────────────────────────────┘
```

核心模块层次：

| 层 | 模块 | 职责 |
|----|------|------|
| 网络层 | muduo `TcpServer` / `TcpClient` | TCP 连接管理、事件驱动 I/O |
| 编解码层 | `ProtobufCodec` + `ProtobufDispatcher` | Protobuf 序列化、按消息类型分发 |
| 连接层 | `Connection` / `ConnectionManager` | 连接生命周期，信道归属管理 |
| 业务层 | `Channel` (客户端/服务端各一) | AMQP 操作：声明、绑定、发布、消费 |
| 调度层 | `VirtualHost` | 聚合四大管理器，统一调度入口 |
| 存储层 | `*Mapper` / `*Manager` | SQLite 元数据 + 文件消息持久化 |
| 路由层 | `Router` | 三种交换机类型的路由匹配算法 |

---

## 📂 项目结构

```
MQ/
├── mqcommon/                  # 公共基础库
│   ├── mq_logger.hpp          #   日志宏（DBG/INF/ERR）
│   ├── mq_helper.hpp          #   SQLite/文件/UUID/字符串工具
│   ├── mq_threadpool.hpp      #   通用线程池
│   ├── mq_msg.proto           #   消息模型（ExchangeType / Message / BasicProperties）
│   └── mq_proto.proto         #   通信协议（12 种请求 + 2 种响应）
├── mqclient/                  # 客户端 SDK
│   ├── mq_worker.hpp          #   AsyncWorker（EventLoop 线程 + 线程池）
│   ├── mq_connection.hpp      #   客户端 Connection（TcpClient + 同步等待）
│   ├── mq_channel.hpp         #   客户端 Channel（同步 API）
│   ├── mq_consumer.hpp        #   消费者回调结构体
│   ├── publish_client.cc      #   发布示例
│   └── consume_client.cc      #   消费示例
├── mqserver/                  # 服务端 Broker
│   ├── mq_server.cc           #   入口
│   ├── mq_broker.hpp          #   Server（监听 + 请求分发 + 生命周期）
│   ├── mq_connection.hpp      #   服务端连接管理
│   ├── mq_channel.hpp         #   服务端 Channel（业务处理）
│   ├── mq_host.hpp            #   VirtualHost（核心调度枢纽）
│   ├── mq_exchange.hpp        #   交换机：数据结构 + SQLite 持久化 + 内存管理
│   ├── mq_queue.hpp           #   队列：数据结构 + SQLite 持久化 + 内存管理
│   ├── mq_binding.hpp         #   绑定关系：数据结构 + SQLite 持久化 + 内存管理
│   ├── mq_message.hpp         #   消息持久化：文件存储 + 逻辑删除 + GC
│   ├── mq_consumer.hpp        #   消费者管理 + Round-Robin 调度
│   └── mq_route.hpp           #   路由引擎：DIRECT/FANOUT/TOPIC 匹配
└── ARCHITECTURE.md            # 详细架构文档
```

---

## 🔌 客户端 API

```cpp
// 1. 初始化
AsyncWorker::ptr awp = std::make_shared<AsyncWorker>();
Connection::ptr conn = std::make_shared<Connection>("127.0.0.1", 8085, awp);
Channel::ptr channel = conn->openChannel();

// 2. 声明交换机
channel->declareExchange("exchange1", TOPIC, /*durable*/true, /*auto_delete*/false, args);

// 3. 声明队列
channel->declareQueue("queue1", /*durable*/true, /*exclusive*/false, /*auto_delete*/false, args);

// 4. 绑定（交换机 ←→ 队列，binding_key 支持通配符）
channel->QueueBind("exchange1", "queue1", "news.music.#");

// 5. 发布消息
BasicProperties bp;
bp.set_id(UUIDHelper::uuid());
bp.set_deliver_mode(DeliverMode::DURABLE);
bp.set_routing_key("news.music.pop");
channel->basicPublish("exchange1", &bp, "Hello World");

// 6. 订阅消费
auto callback = std::bind([](Channel::ptr &c, const string &tag,
    const BasicProperties *bp, const string &body) {
    std::cout << "消费消息: " << body << std::endl;
    c->basicAck(bp->id());  // 手动确认
}, channel, _1, _2, _3);
channel->basicConsumer("consumer1", "queue1", /*auto_ack*/false, callback);

// 7. 关闭
channel->basicCancel();
conn->closeChannel(channel);
```

---

## 🔀 路由匹配

| 交换机类型 | 匹配规则 | 示例 |
|-----------|---------|------|
| `DIRECT` | 精确匹配 routing_key == binding_key | `routing_key = "order.create"` |
| `FANOUT` | 广播到所有绑定队列 | 忽略 routing_key |
| `TOPIC` | `*` 匹配单层，`#` 匹配零或多层 | `"news.#"` 匹配 `"news.sport.music"` |

```
binding: queue1 ← exchange1 (TOPIC, key = "news.#")
binding: queue2 ← exchange1 (TOPIC, key = "*.music.*")

routing_key = "news.music.pop"
  → queue1 ✓ (匹配 "news.#")
  → queue2 ✓ (匹配 "*.music.*")

routing_key = "order.create"
  → queue1 ✗
  → queue2 ✗
```

---

## 🧵 并发模型

```
主线程 (EventLoop)          工作线程池 (threadpool)
┌─────────────────┐        ┌──────────────────────┐
│ TCP I/O 事件循环 │        │ 异步消息推送          │
│ Protobuf 反序列化│ 投递   │ 路由匹配              │
│ 请求分发         │──────▶│ 消费者回调 → 客户端   │
│ 基础响应发送     │        └──────────────────────┘
└─────────────────┘
```

- 各 Manager 内部使用 `std::mutex` 保护共享数据
- 客户端 Channel 通过 `condition_variable` 实现请求-响应的同步等待
- 消费推送在独立线程池中执行，避免阻塞网络 I/O

---

## 🚀 快速开始

### 环境依赖
- GCC 8+ / Clang 10+
- CMake 3.16+
- Protobuf 3.10+
- SQLite3
- muduo

### 构建与运行
```bash
git clone git@github.com:Zhangwho-Fy/MQ.git
cd MQ

mkdir build && cd build
cmake ..
make -j4

# 启动服务端
./bin/mqserver

# 发布消息（新终端）
./bin/publish_client

# 消费消息（新终端，参数为队列名）
./bin/consume_client queue1
```

---

完整架构设计、模块交互时序、持久化格式、通信协议等细节见 [ARCHITECTURE.md](ARCHITECTURE.md)。
