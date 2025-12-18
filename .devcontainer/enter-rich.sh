#!/usr/bin/env bash
set -euo 

docker exec -it -u devgoblin -e TERM=$TERM amp-dev-rich zsh
