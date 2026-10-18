# Atelier Zephyr leger pour ESP32 / ESP32-S3
# Adapte de github.com/ShawnHymel/introduction-to-zephyr (Apache-2.0)

FROM debian:stable-20241016-slim

ARG ZEPHYR_RTOS_COMMIT=26603cefaf41298c417f2eee834ed40d9ab35d3a
ARG ZEPHYR_SDK_VERSION=0.16.8
ARG TOOLCHAIN_LIST="-t xtensa-espressif_esp32_zephyr-elf -t xtensa-espressif_esp32s3_zephyr-elf"

SHELL ["/bin/bash", "-c"]
ENV DEBIAN_FRONTEND=noninteractive

# 1. Outils de base
RUN apt-get update && apt-get install --no-install-recommends -y \
        ca-certificates file locales git build-essential cmake ninja-build \
        gperf device-tree-compiler wget python3 python3-pip python3-venv xz-utils && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN sed -i '/^#.*en_US.UTF-8/s/^#//' /etc/locale.gen && locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# 2. Python isole + west
ENV VIRTUAL_ENV=/opt/venv
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"
RUN pip install --no-cache-dir west

# 3. Zephyr (une seule version, sans historique)
RUN mkdir -p /opt/toolchains/zephyr /workspace && cd /opt/toolchains/zephyr && \
    git init -q && \
    git remote add origin https://github.com/zephyrproject-rtos/zephyr.git && \
    git fetch -q --depth 1 origin ${ZEPHYR_RTOS_COMMIT} && \
    git checkout -q FETCH_HEAD && \
    pip install --no-cache-dir -r scripts/requirements-base.txt

# 4. Seulement les modules utiles a l'ESP32
COPY west.yml /opt/toolchains/zephyr/west.yml
RUN cd /opt/toolchains && \
    west init -l zephyr && \
    west update --narrow -o=--depth=1 && \
    west blobs fetch hal_espressif

# 5. SDK minimal + seulement les compilateurs ESP32 et ESP32-S3
RUN cd /opt/toolchains && \
    wget -q https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz && \
    tar xf zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz && \
    rm zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz && \
    cd zephyr-sdk-${ZEPHYR_SDK_VERSION} && \
    bash setup.sh -c ${TOOLCHAIN_LIST}

# 6. Variables pour que west trouve tout
ENV ZEPHYR_BASE=/opt/toolchains/zephyr
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-${ZEPHYR_SDK_VERSION}

WORKDIR /workspace
CMD ["/bin/bash"]
