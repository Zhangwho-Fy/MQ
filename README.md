# MQ：轻量级消息中间件

一个基于 C++17 实现的轻量级消息队列，兼容 AMQP 核心协议，支持消息持久化、多交换机类型、消费者确认等功能。

---

## ✨ 核心特性
- 🚀 **高性能**：基于 muduo 异步网络库，支持高并发连接与消息分发
- 📦 **多交换机类型**：直连、扇形、主题交换机，支持路由键与通配符匹配
- 💾 **持久化支持**：消息、队列、交换机信息写入 SQLite3，重启不丢失
- ✅ **可靠投递**：消费者确认（ACK/NACK）、死信队列机制，保证消息不丢失
- 🔌 **客户端 SDK**：提供 C++ 客户端，支持同步/异步消费模式

---

## 🛠️ 技术栈
- **语言**：C++17
- **网络库**：muduo
- **序列化**：Protobuf
- **存储**：SQLite3
- **测试框架**：GoogleTest

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
# 克隆仓库
git clone git@github.com:Zhangwho-Fy/MQ.git
cd MQ

# 构建项目
mkdir build && cd build
cmake ..
make -j4

# 启动服务端
./bin/mqserver

# 运行测试客户端（新终端）
./bin/consume_client
./bin/produce_client
```
