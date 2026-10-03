#!/usr/bin/env python3
"""Raw RCON probe: dumps the exact bytes a Minecraft server sends back.

Useful when the manager reports an RCON timeout: it shows whether the server
answered, and with which packet layout.

    python tools/rcon-probe.py 127.0.0.1 25575 <rcon-password> [command]
"""

from __future__ import annotations

import socket
import struct
import sys


def build_packet(request_id: int, packet_type: int, body: bytes) -> bytes:
    return struct.pack("<iii", 10 + len(body), request_id, packet_type) + body + b"\x00\x00"


def receive(sock: socket.socket) -> tuple[int, int, bytes]:
    header = b""
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            raise TimeoutError("connection closed while reading length")
        header += chunk
    (length,) = struct.unpack("<i", header)
    payload = b""
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            raise TimeoutError(f"connection closed after {len(payload)}/{length} payload bytes")
        payload += chunk
    request_id, packet_type = struct.unpack("<ii", payload[:8])
    return request_id, packet_type, payload[8:-2]


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 25575
    password = sys.argv[3] if len(sys.argv) > 3 else ""
    command = sys.argv[4] if len(sys.argv) > 4 else "list"

    sock = socket.create_connection((host, port), timeout=8)
    print(f"connected to {host}:{port}")

    auth = build_packet(1, 3, password.encode("utf-8"))
    print(f"auth packet  ({len(auth)} bytes): {auth.hex(' ')}")
    sock.sendall(auth)
    try:
        request_id, packet_type, body = receive(sock)
        print(f"auth reply   id={request_id} type={packet_type} body={body!r}")
        if request_id == -1:
            print("=> authentication FAILED (wrong password)")
            return 2
    except TimeoutError as error:
        print(f"auth reply   TIMEOUT: {error}")
        return 3

    request = build_packet(2, 2, command.encode("utf-8"))
    print(f"command      {command!r} ({len(request)} bytes): {request.hex(' ')}")
    sock.sendall(request)
    try:
        request_id, packet_type, body = receive(sock)
        print(f"command reply id={request_id} type={packet_type}")
        print("body:")
        print(body.decode("utf-8", "replace"))
    except TimeoutError as error:
        print(f"command reply TIMEOUT: {error}")
        return 3
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
