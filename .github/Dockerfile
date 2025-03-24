# Image to run tt-zephyr-platforms CI tests. Includes Zephyr,
# metal, and other dependencies.

ARG ZEP_CONTAINER=main
FROM ghcr.io/zephyrproject-rtos/zephyr-build:${ZEP_CONTAINER} AS base

LABEL org.opencontainers.image.source=https://github.com/tenstorrent/tt-zephyr-platforms

USER root

# Patch APT urls to use https, since the container can't connect via HTTP
RUN sed -i -e 's/http/https/g' /etc/apt/sources.list && \
    apt-get update

# Enable virtual environments
RUN apt-get install -y python3-venv

# Install Zephyr Cmake package for root
RUN cp /home/user/.cmake/ ~ -r
