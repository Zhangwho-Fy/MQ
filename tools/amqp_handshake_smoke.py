#!/usr/bin/env python3
"""Minimal raw AMQP 0-9-1 client used to smoke-test the server handshake."""

import argparse
import socket
import struct
import sys

AMQP_HEADER = b"AMQP\x00\x00\x09\x01"
FRAME_METHOD = 1
FRAME_END = 0xCE

CONNECTION = 10
START = 10
START_OK = 11
TUNE = 30
TUNE_OK = 31
OPEN = 40
OPEN_OK = 41
CLOSE = 50
CLOSE_OK = 51


def frame(payload: bytes, channel: int = 0, frame_type: int = FRAME_METHOD) -> bytes:
    return struct.pack(">BHI", frame_type, channel, len(payload)) + payload + bytes([FRAME_END])


def method(class_id: int, method_id: int, args: bytes, channel: int = 0) -> bytes:
    return frame(struct.pack(">HH", class_id, method_id) + args, channel)


def read_frame(sock: socket.socket) -> bytes:
    frame_type, payload, channel = read_raw_frame(sock)
    if frame_type != FRAME_METHOD:
        raise AssertionError(f"expected method frame, got {frame_type}")
    return payload, channel


def read_raw_frame(sock: socket.socket):
    header = recv_exact(sock, 7)
    frame_type, channel, size = struct.unpack(">BHI", header)
    payload = recv_exact(sock, size)
    trailer = recv_exact(sock, 1)
    if trailer != bytes([FRAME_END]):
        raise AssertionError("missing frame-end octet")
    return frame_type, payload, channel


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("socket closed")
        chunks.extend(chunk)
    return bytes(chunks)


def shortstr(value: str) -> bytes:
    raw = value.encode()
    if len(raw) > 255:
        raise ValueError("short string too long")
    return bytes([len(raw)]) + raw


def longstr(value: str) -> bytes:
    raw = value.encode()
    return struct.pack(">I", len(raw)) + raw


def start_ok_args() -> bytes:
    client_properties = struct.pack(">I", 0)  # empty field table
    response = b"\x00guest\x00guest"
    return (
        client_properties
        + shortstr("PLAIN")
        + struct.pack(">I", len(response))
        + response
        + shortstr("en_US")
    )


def tune_ok_args(channel_max: int, frame_max: int, heartbeat: int) -> bytes:
    return struct.pack(">HIH", channel_max, frame_max, heartbeat)


def open_args(vhost: str) -> bytes:
    return shortstr(vhost) + b"\x00"


def close_args(reply_code: int = 200) -> bytes:
    return (
        struct.pack(">H", reply_code)
        + shortstr("")
        + struct.pack(">HH", 0, 0)
    )


def exchange_declare_args(name: str, exchange_type: str = "direct") -> bytes:
    return (
        struct.pack(">H", 0)  # ticket
        + shortstr(name)
        + shortstr(exchange_type)
        + b"\x00"  # passive/durable/auto-delete/internal/no-wait bits
        + struct.pack(">I", 0)  # empty arguments table
    )


def queue_declare_args(name: str) -> bytes:
    return (
        struct.pack(">H", 0)  # ticket
        + shortstr(name)
        + b"\x00"  # passive/durable/exclusive/auto-delete/no-wait bits
        + struct.pack(">I", 0)  # empty arguments table
    )


def queue_bind_args(queue: str, exchange: str, routing_key: str) -> bytes:
    return (
        struct.pack(">H", 0)  # ticket
        + shortstr(queue)
        + shortstr(exchange)
        + shortstr(routing_key)
        + b"\x00"  # no-wait bit
        + struct.pack(">I", 0)  # empty arguments table
    )


def basic_publish_args(exchange: str, routing_key: str) -> bytes:
    return (
        struct.pack(">H", 0)  # ticket
        + shortstr(exchange)
        + shortstr(routing_key)
        + b"\x00"  # mandatory/immediate bits
    )


def content_header(body: bytes) -> bytes:
    return (
        struct.pack(">HHQ", 60, 0, len(body))
        + struct.pack(">H", 0)  # no basic properties
    )


def queue_purge_args(queue: str) -> bytes:
    return struct.pack(">H", 0) + shortstr(queue) + b"\x00"


def basic_consume_args(queue: str) -> bytes:
    return (
        struct.pack(">H", 0)  # ticket
        + shortstr(queue)
        + shortstr("")  # server generates consumer tag
        + b"\x00"  # no-ack off, explicit acks
        + struct.pack(">I", 0)  # empty arguments table
    )


def basic_get_args(queue: str) -> bytes:
    return struct.pack(">H", 0) + shortstr(queue) + b"\x00"


def basic_cancel_args(consumer_tag: str) -> bytes:
    return shortstr(consumer_tag) + b"\x00"


def basic_ack_args(delivery_tag: int) -> bytes:
    return struct.pack(">Q", delivery_tag) + b"\x00"


def basic_reject_args(delivery_tag: int, requeue: bool) -> bytes:
    return struct.pack(">Q", delivery_tag) + (b"\x80" if requeue else b"\x00")


def read_delivery_tag(payload: bytes) -> int:
    tag_length = payload[4]
    offset = 5 + tag_length
    return struct.unpack(">Q", payload[offset:offset + 8])[0]


def read_get_delivery_tag(payload: bytes) -> int:
    return struct.unpack(">Q", payload[4:12])[0]


def check_method(payload: bytes, expected_class: int, expected_method: int) -> None:
    class_id, method_id = struct.unpack(">HH", payload[:4])
    if class_id != expected_class or method_id != expected_method:
        raise AssertionError(
            f"expected method {expected_class}:{expected_method}, "
            f"got {class_id}:{method_id}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5672)
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.sendall(AMQP_HEADER)

        payload, _ = read_frame(sock)
        check_method(payload, CONNECTION, START)
        sock.sendall(method(CONNECTION, START_OK, start_ok_args()))

        tune_payload, _ = read_frame(sock)
        check_method(tune_payload, CONNECTION, TUNE)
        channel_max, frame_max, heartbeat = struct.unpack(">HIH", tune_payload[4:])
        sock.sendall(method(CONNECTION, TUNE_OK, tune_ok_args(channel_max, frame_max, heartbeat)))
        sock.sendall(method(CONNECTION, OPEN, open_args("/")))

        payload, _ = read_frame(sock)
        check_method(payload, CONNECTION, OPEN_OK)
        sock.sendall(method(20, 10, b"", channel=1))  # channel.open
        payload, channel = read_frame(sock)
        check_method(payload, 20, 11)  # channel.open-ok
        if channel != 1:
            raise AssertionError(f"open-ok must use channel 1, got {channel}")

        sock.sendall(method(40, 10, exchange_declare_args("logs", "fanout"), channel=1))
        payload, channel = read_frame(sock)
        check_method(payload, 40, 11)  # exchange.declare-ok

        sock.sendall(method(50, 10, queue_declare_args("q1"), channel=1))
        payload, channel = read_frame(sock)
        check_method(payload, 50, 11)  # queue.declare-ok

        sock.sendall(method(50, 20, queue_bind_args("q1", "logs", "task"), channel=1))
        payload, channel = read_frame(sock)
        check_method(payload, 50, 21)  # queue.bind-ok

        sock.sendall(method(60, 20, basic_consume_args("q1"), channel=1))
        payload, channel = read_frame(sock)
        check_method(payload, 60, 21)  # basic.consume-ok
        tag_length = payload[4]
        consumer_tag = payload[5:5 + tag_length].decode()

        body = b"hello"
        sock.sendall(method(60, 40, basic_publish_args("", "q1"), channel=1))
        sock.sendall(frame(content_header(body), channel=1, frame_type=2))
        sock.sendall(frame(body, channel=1, frame_type=3))

        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 1:
            raise AssertionError(f"expected basic.deliver, got frame {frame_type}")
        check_method(payload, 60, 60)  # basic.deliver
        first_tag = read_delivery_tag(payload)
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 2:
            raise AssertionError(f"expected content header, got frame {frame_type}")
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 3 or payload != body:
            raise AssertionError("content body mismatch")
        sock.sendall(method(60, 80, basic_ack_args(first_tag), channel=1))

        sock.sendall(method(60, 40, basic_publish_args("", "q1"), channel=1))
        sock.sendall(frame(content_header(body), channel=1, frame_type=2))
        sock.sendall(frame(body, channel=1, frame_type=3))
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 1:
            raise AssertionError(f"expected second basic.deliver, got {frame_type}")
        check_method(payload, 60, 60)
        second_tag = read_delivery_tag(payload)
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 2:
            raise AssertionError("expected second content header")
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 3:
            raise AssertionError("expected second content body")
        sock.sendall(method(60, 90, basic_reject_args(second_tag, True), channel=1))
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 1:
            raise AssertionError(f"expected redelivery, got frame {frame_type}")
        check_method(payload, 60, 60)
        redelivered_tag = read_delivery_tag(payload)
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 2:
            raise AssertionError("expected redelivered content header")
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 3:
            raise AssertionError("expected redelivered content body")
        sock.sendall(method(60, 80, basic_ack_args(redelivered_tag), channel=1))

        sock.sendall(method(60, 30, basic_cancel_args(consumer_tag), channel=1))
        payload, channel = read_frame(sock)
        check_method(payload, 60, 31)  # basic.cancel-ok

        sock.sendall(method(60, 40, basic_publish_args("", "q1"), channel=1))
        sock.sendall(frame(content_header(b"third"), channel=1, frame_type=2))
        sock.sendall(frame(b"third", channel=1, frame_type=3))
        sock.sendall(method(60, 70, basic_get_args("q1"), channel=1))
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 1:
            raise AssertionError(f"expected basic.get-ok, got {frame_type}")
        check_method(payload, 60, 71)  # basic.get-ok
        get_tag = read_get_delivery_tag(payload)
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 2:
            raise AssertionError("expected get content header")
        frame_type, payload, channel = read_raw_frame(sock)
        if frame_type != 3 or payload != b"third":
            raise AssertionError("get body mismatch")
        sock.sendall(method(60, 80, basic_ack_args(get_tag), channel=1))

        sock.sendall(method(50, 30, queue_purge_args("q1"), channel=1))
        payload, channel = read_frame(sock)
        check_method(payload, 50, 31)  # queue.purge-ok
        purged = struct.unpack(">I", payload[4:8])[0]
        if purged != 0:
            raise AssertionError(f"expected zero purged messages, got {purged}")

        # basic.qos is not implemented yet; it must close only channel 1.
        sock.sendall(method(60, 10, b"", channel=1))
        payload, channel = read_frame(sock)
        check_method(payload, 20, 40)  # channel.close
        reply_code = struct.unpack(">H", payload[4:6])[0]
        if reply_code != 540:
            raise AssertionError(f"expected not-implemented 540, got {reply_code}")
        sock.sendall(method(20, 41, b"", channel=1))  # channel.close-ok

        sock.sendall(method(CONNECTION, CLOSE, close_args()))
        payload, _ = read_frame(sock)
        check_method(payload, CONNECTION, CLOSE_OK)

    print("AMQP handshake smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
