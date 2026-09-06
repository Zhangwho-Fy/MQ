#!/usr/bin/env python3
"""Real pika client smoke test against the local AMQP server."""

import argparse
import sys
import time

import pika


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5672)
    args = parser.parse_args()

    credentials = pika.PlainCredentials("guest", "guest")
    params = pika.ConnectionParameters(
        host=args.host,
        port=args.port,
        credentials=credentials,
        heartbeat=5,
    )
    connection = pika.BlockingConnection(params)
    channel = connection.channel()

    channel.exchange_declare(
        exchange="pika_logs", exchange_type="fanout", durable=True
    )
    channel.queue_declare(queue="pika_q", durable=True)
    channel.queue_bind(queue="pika_q", exchange="pika_logs", routing_key="")

    channel.basic_qos(prefetch_count=1)
    channel.confirm_delivery()

    received = []

    def on_message(_channel, method, _properties, body):
        received.append(body)
        _channel.basic_ack(delivery_tag=method.delivery_tag)

    tag = channel.basic_consume(
        queue="pika_q", on_message_callback=on_message, auto_ack=False
    )
    channel.basic_publish(
        exchange="pika_logs",
        routing_key="",
        body=b"hello from pika",
        properties=pika.BasicProperties(delivery_mode=2),
    )

    deadline = time.time() + 5
    while not received and time.time() < deadline:
        connection.process_data_events(time_limit=0.5)
    if not received:
        print("pika interop failed: message not delivered")
        return 1
    if received[0] != b"hello from pika":
        print(f"pika interop failed: unexpected body {received[0]!r}")
        return 1

    channel.basic_cancel(consumer_tag=tag)
    connection.close()
    print("pika interop smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
