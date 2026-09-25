################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.

from .sdk import EXTERNAL_KEY_MIN, ExternalKey, ProducerIdentityStatus, external_key, is_external_key

try:
    from perception_bridge import Envelope
except ModuleNotFoundError as exc:
    if exc.name != "perception_bridge":
        raise
    raise ImportError(
        "open_perception_kit.guest is available only inside a C++ host that registered "
        "the generated perception_bridge module"
    ) from exc

__all__ = [
    "EXTERNAL_KEY_MIN",
    "Envelope",
    "ExternalKey",
    "ProducerIdentityStatus",
    "external_key",
    "is_external_key",
]
