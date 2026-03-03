################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import logging
import os

from expkits_ci.file_utils import FileUtils

logger = logging.getLogger("expkits_ci")


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
                    self.templates[key] = f.read().rstrip() + '\n'
                    logger.debug(
                        f"{key.capitalize()} license template loaded: {self.templates[key]}")
            except Exception as e:
                logger.error(f"Could not load {key} license template: {e}")
                return None
        return self.templates[key]
