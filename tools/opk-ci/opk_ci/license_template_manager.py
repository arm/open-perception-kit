# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import logging
import os
from datetime import date

from opk_ci.file_utils import FileUtils

logger = logging.getLogger("opk_ci")


class LicenseTemplateManager:
    """Manages license templates for different file types."""
    TEMPLATE_ROOT = os.path.join(FileUtils.get_project_root(), "tools", "templates", "header")

    def __init__(self):
        """Initialize the LicenseTemplateManager."""
        self.templates = {}

    def get(self, key):
        """Get the license template for the specified key(file extension)."""
        if key not in self.templates:
            try:
                with open(os.path.join(self.TEMPLATE_ROOT, f"{key}.template"), 'r', encoding='utf-8') as f:
                    self.templates[key] = f.read().replace('[year]', str(date.today().year)).rstrip() + '\n'
                    logger.debug(
                        f"{key.capitalize()} license template loaded: {self.templates[key]}")
            except Exception as e:
                logger.error(f"Could not load {key} license template: {e}")
                return None
        return self.templates[key]
