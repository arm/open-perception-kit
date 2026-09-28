################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.

from .sdk import EXTERNAL_KEY_MIN, ExternalKey, ProducerIdentityStatus, external_key, is_external_key

try:
    from open_perception_kit_bridge import Envelope
except ModuleNotFoundError as exc:
    if exc.name != "open_perception_kit_bridge":
        raise
    raise ImportError(
        "open_perception_kit.guest is available only inside a C++ host that registered "
        "the generated open_perception_kit_bridge module"
    ) from exc

__all__ = [
    "EXTERNAL_KEY_MIN",
    "Envelope",
    "ExternalKey",
    "ProducerIdentityStatus",
    "external_key",
    "is_external_key",
]
