#!/bin/sh

# shellcheck disable=SC2086
set -e

# sudo tls/0certificates.sh
sudo useradd -u 2000 subscriber    || true
sudo chown 2000:2000 tls/subscriber.* || true
if [ "$1" = "clean" ] || [ "$1" = "clear" ]; then
    sudo docker compose down -v
fi
sudo docker compose up --build  --force-recreate subscriber 
