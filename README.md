# MQ：AMQP 0-9-1 轻量级消息中间件

基于 C++17 实现的 RabbitMQ 风格轻量消息队列，直接使用 AMQP 0-9-1 二进制协议，支持持久化、多交换机类型、消费者确认、DLX、TTL、QoS 和真实客户端互操作。

## 核心能力

- AMQP 0-9-1 帧、连接握手、信道协议
- direct / fanout / topic 交换机与绑定
- publish / consume / basic.get
- ack / reject / requeue / redelivered
- DLX 与队列级/消息级 TTL
- heartbeat 超时
- basic.qos prefetch
- no-local、exclusive consumer
- exclusive / auto-delete 队列生命周期
- SQLite 元数据 + 队列消息日志持久化
- pika 真实客户端互操作验证

## 技术栈

- C++17
- muduo（网络）
- SQLite3（元数据）
- GoogleTest（测试）
- pika（互操作冒烟）

## 快速开始

依赖：

```bash
sudo apt-get install -y cmake g++ libboost-dev libprotobuf-dev protobuf-compiler libsqlite3-dev libgtest-dev
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

启用管理 HTTP API：

```bash
./build/amqp_server -p 5672 --http-port 15672 --data ./data
curl http://127.0.0.1:15672/api/overview
```

pika 互操作冒烟：

```bash
python3 tools/pika_interop_smoke.py --port 5672
```

测试：

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

API 文档见 `docs/openapi.yaml`，可用浏览器打开 `docs/swagger-ui.html` 预览。

## 说明

默认账号为 `guest/guest`，virtual host 为 `/`。当前目标是功能完整的轻量 AMQP 服务器，适合学习和内部试用，尚未覆盖事务、publisher confirm、headers 交换机、多 vhost 与生产级运维能力。
