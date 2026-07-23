ARG STEAMRT_BASE_IMAGE=registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest
FROM ${STEAMRT_BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf-archive \
    gettext \
    autopoint \
    cargo \
    clang-format \
    libltdl-dev \
    python3-venv \
    rustc \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg-seed \
    && /opt/vcpkg-seed/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

COPY run-build.sh /usr/local/bin/steamrt-build
RUN chmod +x /usr/local/bin/steamrt-build

WORKDIR /work/ofpr
ENTRYPOINT ["/usr/local/bin/steamrt-build"]
