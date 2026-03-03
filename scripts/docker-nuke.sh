#!/bin/bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


set -e

echo "🚫 Stopping all running Docker containers..."
docker ps -q | xargs -r docker stop

echo "🧹 Removing all Docker containers..."
docker ps -a -q | xargs -r docker rm

echo "🧨 Removing all Docker images..."
docker images -q | xargs -r docker rmi -f

echo "✅ Done! All containers and images removed."
