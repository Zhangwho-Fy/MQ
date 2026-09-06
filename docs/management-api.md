# MQ 管理 HTTP API

管理 API 提供只读统计和队列管理能力，风格参考 RabbitMQ Management HTTP API。默认不开启，启动时通过 `--http-port` 启用。

## 启动方式

```bash
./build/amqp_server -p 5672 --http-port 15672 --data ./data
```

请求和响应均为 JSON，`Content-Type: application/json`。

## 接口一览

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/overview` | 服务器概况 |
| GET | `/api/exchanges` | 交换机列表 |
| GET | `/api/queues` | 队列列表 |
| GET | `/api/queues/{queue}` | 单个队列详情 |
| DELETE | `/api/queues/{queue}` | 删除队列 |

## GET /api/overview

示例响应：

```json
{
  "connections": 0,
  "queues": 1,
  "exchanges": 1
}
```

字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| connections | number | 当前 AMQP 连接数 |
| queues | number | 队列总数 |
| exchanges | number | 交换机总数 |

## GET /api/exchanges

示例响应：

```json
[
  {
    "name": "pika_logs",
    "type": "fanout",
    "durable": true,
    "auto_delete": false,
    "internal": false
  }
]
```

## GET /api/queues

示例响应：

```json
[
  {
    "name": "pika_q",
    "message_count": 0,
    "consumer_count": 0,
    "durable": true,
    "exclusive": false,
    "auto_delete": false,
    "dead_letter_exchange": "dlx",
    "message_ttl_ms": 60000
  }
]
```

字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| name | string | 队列名 |
| message_count | number | 当前可投递消息数 |
| consumer_count | number | 当前消费者数 |
| durable | boolean | 是否持久队列 |
| exclusive | boolean | 是否独占队列 |
| auto_delete | boolean | 是否自动删除 |
| dead_letter_exchange | string | 死信交换机，空表示未配置 |
| message_ttl_ms | number | 队列级 TTL，0 表示不限制 |

## GET /api/queues/{queue}

队列不存在返回 `404`：

```json
{ "error": "queue not found" }
```

## DELETE /api/queues/{queue}

成功：

```json
{ "deleted": true }
```

队列不存在返回 `404`。

## 错误响应

未知路径返回：

```json
{ "error": "not found" }
```

HTTP 状态码：

| 状态码 | 含义 |
| --- | --- |
| 200 | 成功 |
| 404 | 路径或队列不存在 |

## 规划中的接口

以下接口尚未实现，可按需补充：

```text
GET    /api/connections
DELETE /api/exchanges/{exchange}
POST   /api/queues/{vhost}/{queue}
```

