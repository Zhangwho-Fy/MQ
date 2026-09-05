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


def frame(payload: bytes) -> bytes:
    return struct.pack(">BHI", FRAME_METHOD, 0, len(payload)) + payload + bytes([FRAME_END])


def method(class_id: int, method_id: int, args: bytes) -> bytes:
    return frame(struct.pack(">HH", class_id, method_id) + args)


def read_frame(sock: socket.socket) -> bytes:
    header = recv_exact(sock, 7)
    frame_type, channel, size = struct.unpack(">BHI", header)
    if frame_type != FRAME_METHOD:
        raise AssertionError(f"expected method frame, got {frame_type}")
    payload = recv_exact(sock, size)
    trailer = recv_exact(sock, 1)
    if trailer != bytes([FRAME_END]):
        raise AssertionError("missing frame-end octet")
    if channel != 0:
        raise AssertionError("connection method must use channel 0")
    return payload


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


def check_method(payload: bytes, expected_method: int) -> None:
    class_id, method_id = struct.unpack(">HH", payload[:4])
    if class_id != CONNECTION or method_id != expected_method:
        raise AssertionError(
            f"expected connection method {expected_method}, got {class_id}:{method_id}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5672)
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.sendall(AMQP_HEADER)

        check_method(read_frame(sock), START)
        sock.sendall(method(CONNECTION, START_OK, start_ok_args()))

        tune_payload = read_frame(sock)
        check_method(tune_payload, TUNE)
        channel_max, frame_max, heartbeat = struct.unpack(">HIH", tune_payload[4:])
        sock.sendall(method(CONNECTION, TUNE_OK, tune_ok_args(channel_max, frame_max, heartbeat)))
        sock.sendall(method(CONNECTION, OPEN, open_args("/")))

        check_method(read_frame(sock), OPEN_OK)
        sock.sendall(method(CONNECTION, CLOSE, close_args()))
        check_method(read_frame(sock), CLOSE_OK)

    print("AMQP handshake smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
