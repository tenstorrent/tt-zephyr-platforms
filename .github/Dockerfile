# Image to run tt-zephyr-platforms CI tests. Includes Zephyr,
# metal, and other dependencies.

ARG ZEP_CONTAINER=main
FROM ghcr.io/zephyrproject-rtos/zephyr-build:${ZEP_CONTAINER} AS base

USER root

# Patch APT urls to use https, since the container can't connect via HTTP
RUN sed -i -e 's/http/https/g' /etc/apt/sources.list && \
    apt-get update

# Enable virtual environments
RUN apt-get install -y python3-venv

# Install Zephyr Cmake package for root
RUN cp /home/user/.cmake/ ~ -r

# Start a new stage- the build artifacts are not needed in the final image
FROM base AS builder

ARG REPO_REV=main
ARG REPO_URL=https://github.com/tenstorrent/tt-zephyr-platforms

# Clone the tt-zephyr-platforms repo
RUN pip install west && \
    west init -m ${REPO_URL} /tt-zephyr && \
    cd /tt-zephyr/tt-zephyr-platforms && \
    git config --global user.email "you@example.com" && \
    git config --global user.name "Your Name" && \
    rm -fr ".git/rebase-apply" && \
    rm -fr ".git/rebase-merge" && \
    git fetch origin ${REPO_REV} && \
    git checkout FETCH_HEAD && \
    git rebase origin/main && \
    west config manifest.group-filter -- +ci && \
    west update

# Install clang-17 to build metal (we need to manually patch the apt sources to use https)
RUN mkdir /tmp/llvm-build && \
    cd /tmp/llvm-build && \
    wget https://apt.llvm.org/llvm.sh && \
    sed -i "s/http:\/\/apt.llvm.org/https:\/\/apt.llvm.org/" -i llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 17 all && \
    cd ~ && \
    rm -rf /tmp/llvm-build

# Install metal dependencies
RUN cd /tt-zephyr/ci/tt-metal && \
    sed -i "s/http:\/\/apt.llvm.org/https:\/\/apt.llvm.org/" -i install_dependencies.sh && \
    ./install_dependencies.sh --docker --mode build

# Update cmake to 3.30.0 (not the latest, so we use apt-mark hold to prevent it from being updated)
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null && \
    echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null && \
    apt-get update && \
    apt-get install -y --allow-downgrades cmake=3.30.8-0kitware1ubuntu22.04.1 cmake-data=3.30.8-0kitware1ubuntu22.04.1 && \
    apt-mark hold cmake cmake-data

# Build metal
RUN cp -r /tt-zephyr/ci/tt-metal /tt-metal && \
    cd /tt-metal && \
    git submodule update --init --recursive && \
    ./build_metal.sh --build-tests --build-programming-examples

# Setup reusable metal python environment
RUN cd /tt-metal && \
    PYTHON_ENV_DIR=/metal-env source ./create_venv.sh

# Build openocd revision with RTT support
RUN cd /tt-zephyr/ci/openocd && \
    ./bootstrap && \
    ./configure --prefix=/opt/openocd && \
    make -j$(nproc) && \
    make install

# Copy the build environments and metal to a new stage
FROM base

COPY --from=builder /metal-env /metal-env
COPY --from=builder /opt/openocd /opt/openocd
COPY --from=builder /tt-metal /tt-metal

RUN cd /tt-metal && \
    ./install_dependencies.sh --docker --mode runtime
