#!/usr/bin/env bash
# One-time setup for the LectureLens dev environment on Ubuntu 24.04 (WSL2).
# Run it yourself: it asks for your sudo password.
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build git pkg-config curl ca-certificates \
  libpoppler-cpp-dev poppler-utils \
  libpqxx-dev libpq-dev postgresql-client \
  nlohmann-json3-dev libcpp-httplib-dev libcurl4-openssl-dev libssl-dev \
  libgtest-dev libgmock-dev libbenchmark-dev \
  libopencv-dev \
  python3-venv \
  docker.io docker-compose-v2

sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"

echo
echo "Done. Run 'wsl --shutdown' from Windows (or close all WSL windows) once,"
echo "so the docker group membership takes effect."
