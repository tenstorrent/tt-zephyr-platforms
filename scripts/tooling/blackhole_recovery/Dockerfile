# Image to run blackhole recovery tooling.
# Includes python dependencies, a built recovery bundle, and
# necessary recovery scripts

ARG ZEP_CONTAINER=v0.28.2
FROM ghcr.io/zephyrproject-rtos/zephyr-build:${ZEP_CONTAINER} AS build-image

USER root

# Patch APT urls to use https, since the container can't connect via HTTP
RUN sed -i -e 's/http/https/g' /etc/apt/sources.list && \
    apt-get update

# Install Zephyr Cmake package for root
RUN cp /home/user/.cmake/ ~ -r

ARG REPO_REV=main
ARG REPO_URL=https://github.com/tenstorrent/tt-zephyr-platforms

ENV ZEPHYR_BASE=/tt-zephyr/zephyr

# Clone the tt-zephyr-platforms repo and install dependencies
RUN pip install west && \
    west init -m ${REPO_URL} --mr ${REPO_REV} /tt-zephyr && \
    cd /tt-zephyr/tt-zephyr-platforms && \
    west update && \
    west packages pip --install

# Build the blackhole recovery image
RUN cd /tt-zephyr/tt-zephyr-platforms && \
    west patch apply && \
    python3 ./scripts/tooling/blackhole_recovery/prepare-recovery-bundle.py /recovery.tar.gz

# Start a new stage, and copy the built recovery bundle
FROM ubuntu:24.04
LABEL org.opencontainers.image.source=https://github.com/tenstorrent/tt-zephyr-platforms
COPY --from=build-image /recovery.tar.gz /recovery.tar.gz

ARG PYTHON_VENV_PATH=/opt/python/venv
ARG REPO_REV=main
ARG REPO_URL=https://github.com/tenstorrent/tt-zephyr-platforms

# Install python and create a venv
RUN apt-get update && \
    apt-get install -y python3 python3-pip python3-venv && \
	mkdir -p ${PYTHON_VENV_PATH} && \
	python3 -m venv ${PYTHON_VENV_PATH}

# Make python venv available in PATH
ENV PATH=${PYTHON_VENV_PATH}/bin:$PATH

# Install usb dependencies
RUN apt-get install -y libusb-1.0-0

# Install python dependencies in the venv
RUN pip install pyocd pyluwen pyyaml pykwalify protobuf && \
    pyocd pack install STM32G0B1CEUx

# Clone the tt-zephyr-platforms repo to get recovery scripts
RUN apt-get install -y git && \
    git clone --branch ${REPO_REV} ${REPO_URL} /tt-zephyr-platforms
