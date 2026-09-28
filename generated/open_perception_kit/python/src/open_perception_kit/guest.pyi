################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.

from typing import TypeVar, overload

from .sdk import ExternalKey, ProducerIdentityStatus, external_key, is_external_key

_PayloadT = TypeVar("_PayloadT")


class Envelope:
    @property
    def producer_sdk_name(self) -> str: ...

    @property
    def producer_sdk_version(self) -> str: ...

    @property
    def producer_schema_set_sha256(self) -> str: ...

    def valid(self) -> bool: ...
    def producer_identity(self) -> ProducerIdentityStatus: ...

    @overload
    def count(self, selector: type[_PayloadT]) -> int: ...
    @overload
    def count(self, selector: ExternalKey) -> int: ...

    @overload
    def contains(self, selector: type[_PayloadT]) -> bool: ...
    @overload
    def contains(self, selector: ExternalKey) -> bool: ...

    @overload
    def get(self, selector: type[_PayloadT], index: int = 0) -> _PayloadT | None: ...
    @overload
    def get(self, selector: ExternalKey, index: int = 0) -> bytes | None: ...

    @overload
    def for_each(self, selector: type[_PayloadT]) -> list[_PayloadT]: ...
    @overload
    def for_each(self, selector: ExternalKey) -> list[bytes]: ...

    @overload
    def add(self, value: _PayloadT) -> None: ...

    @overload
    def add(
        self,
        value: ExternalKey,
        blob: bytes | bytearray | memoryview,
    ) -> None: ...
