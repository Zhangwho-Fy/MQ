# AMQP 0-9-1 API 设计

## 设计边界

AMQP 不是 REST API。REST 通常使用 HTTP 方法、URL 和 JSON；AMQP 客户端和服务器之间使用长连接、信道和二进制 method/content frame。对外兼容性由 AMQP wire protocol 决定，不能通过设计一组 REST URL 获得。

本项目保留两层接口：

| 层 | 面向对象 | 形式 | 作用 |
| --- | --- | --- | --- |
| AMQP 对外接口 | RabbitMQ、pika 等标准客户端 | AMQP 0-9-1 二进制帧 | 对外兼容协议 |
| 项目内部接口 | connection session、broker、测试 | C++ 类型和函数 | 解码后的命令与 broker 操作 |

Protobuf 暂时只属于内部接口，不能出现在 AMQP TCP 端口的输入输出路径中。迁移期间可以保留现有 Protobuf 客户端和测试，但它们应与 AMQP session 分离。

## 当前已实现的协议层 API

头文件位于 `include/mq/protocol/amqp091`，库 target 为 `amqp091_protocol`。

### Frame

```cpp
namespace mq::amqp091 {

struct Frame {
    uint8_t type;
    uint16_t channel;
    std::string payload;
};

}
```

合法帧类型常量：`kFrameMethod`（1）、`kFrameHeader`（2）、`kFrameBody`（3）和 `kFrameHeartbeat`（8）。帧结束字节为 `kFrameEnd`（`0xCE`）。

### FrameEncoder

```cpp
std::string FrameEncoder::encode(const Frame& frame, uint32_t frame_max = 0);
```

把 Frame 编码为 AMQP wire bytes。`frame_max=0` 表示不设置本地最大值；指定非零值时，完整帧大小不能超过该值。遇到未知帧类型、非法 heartbeat、payload 过大或超过上限时抛出 `FrameCodecError`。

### FrameDecoder

```cpp
explicit FrameDecoder(uint32_t frame_max = 131072);
DecodeResult feed(std::string_view bytes, std::vector<Frame>& frames);
void reset();
```

`feed` 可以被 Muduo 的 TCP read callback 多次调用。它会保留不完整的帧头或 payload，并在收到完整帧后追加到 `frames`。一次调用可以产生零个、一个或多个 Frame。

返回状态：

| 状态 | 含义 |
| --- | --- |
| `kOk` | 本次输入成功解析，`frames_decoded` 表示新增帧数 |
| `kNeedMoreData` | 当前输入还不足以组成完整帧 |
| `kError` | 帧类型、长度、结束字节或 heartbeat 规则非法；错误信息在 `error` |

发生不可恢复错误后，decoder 会保持错误状态；调用 `reset()` 才能开始新的字节流。

## AMQP 方法 API 规划

下一层 session 将把 method frame 的四字节 `(class-id, method-id)` 和参数解析成类型化命令。命令不直接暴露 TCP 字节，而是调用 broker service。

| AMQP 类 | 关键方法 | 内部服务方向 |
| --- | --- | --- |
| `connection` | `start`、`start-ok`、`tune`、`tune-ok`、`open`、`close` | 建立/关闭 session、认证、协商参数 |
| `channel` | `open`、`flow`、`close` | 管理信道状态和流控 |
| `exchange` | `declare`、`delete` | 调用 exchange service |
| `queue` | `declare`、`bind`、`unbind`、`purge`、`delete` | 调用 queue/binding service |
| `basic` | `qos`、`consume`、`cancel`、`publish`、`get`、`ack`、`reject`、`recover` | 调用消息投递 service |
| `tx` | `select`、`commit`、`rollback` | 调用 channel transaction service |

每个方法处理器都必须明确：

1. 接收方向（客户端到服务器，或服务器到客户端）。
2. 所属信道，连接级方法使用 channel 0。
3. 参数类型和字段顺序。
4. 是否同步，以及对应的 `*-ok` 响应。
5. 错误范围：channel error 还是 connection error。
6. 是否伴随 content header/body frame。

## 不应设计成 REST 的部分

- 不为 `queue.declare`、`basic.publish` 等方法设计 HTTP URL。
- 不把 AMQP method 参数先转换成 JSON 再传输。
- 不让 broker 代码直接读取 socket buffer；必须先经过 FrameDecoder 和 session 状态机。
- 不让帧编解码器依赖 Protobuf、SQLite 或 Muduo。

## 相关规范章节

- AMQP 规范第 1.2 节：帧类型常量、`frame-min-size`、`frame-end` 和错误码。
- 第 1.4 节：connection 方法和连接语法。
- 第 1.5 节：channel 方法和 flow 控制。
- 第 1.8 节：basic content、消息属性和投递方法。
- 第 1.9 节：事务方法。

## Stage 1 完成标准

- FrameEncoder/FrameDecoder 能处理完整帧、分片帧和连续多帧。
- 支持 method/header/body/heartbeat 四种帧类型。
- 强制校验 `frame-max`、`frame-end` 和 heartbeat 规则。
- 二进制 payload 不受 UTF-8 或 null 字节影响。
- `frame_codec_test` 和 `wire_types_test` 通过。
- 协议库不依赖 broker、SQLite、Protobuf 或 Muduo。

