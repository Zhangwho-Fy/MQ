# MQ：AMQP 0-9-1 轻量级消息中间件

基于 C++17 实现的 RabbitMQ 风格轻量消息队列，使用标准 AMQP 0-9-1 二进制协议，支持持久化、多交换机、消费者确认、DLX/TTL、publisher confirm、多 vhost 与 HTTP 管理接口，并已通过 pika 真实客户端互操作验证。

## 核心能力

- AMQP 0-9-1 帧、连接握手、信道协议
- direct / fanout / topic 交换机与绑定
- publish / consume / basic.get / recover
- ack / reject / requeue / redelivered
- publisher confirm
- Basic 属性透传
- DLX、队列级/消息级 TTL
- heartbeat 超时
- basic.qos prefetch
- no-local、exclusive consumer
- exclusive / auto-delete 队列生命周期
- SQLite 元数据 + 队列消息日志持久化与日志压缩
- 多线程 IO、VirtualHost 并发保护
- 多 vhost + PLAIN 认证（PBKDF2）
- 管理 HTTP API
- graceful shutdown

## 技术栈

- C++17
- muduo（网络 / HTTP）
- SQLite3（元数据）
- GoogleTest（测试）
- pika（互操作冒烟）

## 快速开始

依赖：

```bash
sudo apt-get install -y cmake g++ libboost-dev libsqlite3-dev libgtest-dev
```

构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
```

运行：

```bash
./build/amqp_server -p 5672 --data ./data
```

同时启用管理 HTTP API：

```bash
./build/amqp_server \
  -p 5672 \
  --http-port 15672 \
  --data ./data
```

默认用户 `guest/guest`，可访问 vhost `/`。

配置更多用户与 vhost：

```bash
MQ_USERS='alice:secret:/alice;bob:pass:/bob' \
  ./build/amqp_server -p 5672 --data ./data
```

### pika 互操作

```bash
python3 -m pip install pika
python3 tools/pika_interop_smoke.py --port 5672
```

### 管理 API 示例

```bash
curl -u guest:guest \
     -H 'X-Virtual-Host: /' \
     http://127.0.0.1:15672/api/overview

curl -u alice:secret \
     -H 'X-Virtual-Host: /alice' \
     http://127.0.0.1:15672/api/queues
```

### 测试

```bash
ctest --test-dir build --output-on-failure
```

## 目录

```text
include/mq/    broker/protocol/transport 头文件
src/           实现
tests/         单元与协议测试
tools/         AMQP raw/pika 冒烟客户端
common/        公共日志
mqthird/       muduo 静态库
```

文档：

- 管理 API：`docs/openapi.yaml`，可打开 `docs/swagger-ui.html` 预览
- 架构与调用流程：`docs/architecture-and-flows.md`（本地文档）

## 说明

默认账号为 `guest/guest`，virtual host 为 `/`。项目目标是功能完整的轻量 AMQP 服务器，适合学习和内部试用。

尚未覆盖：headers exchange、tx 事务、用户数据持久化、TLS、集群复制与完整监控告警。这些属于生产化扩展，不属于当前重构范围。

