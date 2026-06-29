################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Thin TCP client for AMP newline-delimited JSON transport messages."""

from __future__ import annotations

import json
import socket
from dataclasses import dataclass
from typing import Iterator, Optional


class JsonStreamError(RuntimeError):
    """Raised when the JSON stream transport or payload is invalid."""


@dataclass
class JsonStreamClient:
    """Receive newline-delimited JSON objects over TCP."""

    host: str = "127.0.0.1"
    port: int = 7001
    timeout_s: Optional[float] = None
    recv_size: int = 65536

    def __post_init__(self) -> None:
        if not self.host:
            raise ValueError("host must not be empty")
        if not (0 < int(self.port) < 65536):
            raise ValueError("port must be in range 1..65535")
        if self.recv_size <= 0:
            raise ValueError("recv_size must be positive")
        self._sock: Optional[socket.socket] = None
        self._buffer = bytearray()

    def connect(self) -> "JsonStreamClient":
        if self._sock is not None:
            return self

        try:
            sock = socket.create_connection((self.host, self.port), timeout=self.timeout_s)
        except OSError as exc:
            raise JsonStreamError(f"failed to connect to {self.host}:{self.port}: {exc}") from exc

        if self.timeout_s is not None:
            sock.settimeout(self.timeout_s)

        self._sock = sock
        self._buffer.clear()
        return self

    def close(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None
        self._buffer.clear()

    def __enter__(self) -> "JsonStreamClient":
        return self.connect()

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def iter_messages(self) -> Iterator[dict]:
        self.connect()

        while True:
            line = self._readline()
            if line is None:
                return

            text = line.decode("utf-8").strip()
            if not text:
                continue

            try:
                payload = json.loads(text)
            except json.JSONDecodeError as exc:
                raise JsonStreamError(f"invalid JSON payload: {exc.msg}") from exc

            if not isinstance(payload, dict):
                raise JsonStreamError("expected each JSON message to be an object")

            yield payload

    def _readline(self) -> Optional[bytes]:
        while True:
            newline_idx = self._buffer.find(b"\n")
            if newline_idx >= 0:
                line = bytes(self._buffer[:newline_idx])
                del self._buffer[: newline_idx + 1]
                return line

            chunk = self._recv_chunk()
            if chunk == b"":
                if self._buffer:
                    raise JsonStreamError("stream ended with a partial JSON line")
                return None

            self._buffer.extend(chunk)

    def _recv_chunk(self) -> bytes:
        if self._sock is None:
            raise JsonStreamError("socket is not connected")

        try:
            return self._sock.recv(self.recv_size)
        except OSError as exc:
            raise JsonStreamError(f"failed while receiving JSON stream: {exc}") from exc
