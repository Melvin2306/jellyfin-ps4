# PS4 (OpenOrbis) build environment for Jellyfin PS4.
# Only official sources, each pinned by sha256:
#  - Ubuntu 24.04 + its apt packages (clang/lld/llvm 18, nasm)
#  - libssl1.1 from archive.ubuntu.com (PkgTool.Core needs OpenSSL 1.1)
#  - OpenOrbis PS4 Toolchain v0.5.4 official GitHub release
#    (sha256 identical to the digest GitHub reports for the release asset)
# Run as linux/amd64 (the toolchain's create-fself/PkgTool are x86-64 Linux).
FROM ubuntu:24.04

ARG OPENORBIS_SHA256=3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526
ARG LIBSSL_DEB=libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb
ARG LIBSSL_SHA256=7cf39d70a639017d1dd7c8d36daa2258063608688e449fddf40ffdd46f992a78

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        clang-18 lld-18 llvm-18 libc6-dev make nasm pkg-config curl ca-certificates xz-utils python3 file \
    && ln -s /usr/bin/clang-18 /usr/local/bin/clang \
    && ln -s /usr/bin/clang++-18 /usr/local/bin/clang++ \
    && ln -s /usr/bin/ld.lld-18 /usr/local/bin/ld.lld \
    && for t in llvm-ar llvm-ranlib llvm-nm llvm-strip; do ln -s /usr/bin/$t-18 /usr/local/bin/$t; done \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/libssl.deb http://archive.ubuntu.com/ubuntu/pool/main/o/openssl/${LIBSSL_DEB} \
    && echo "${LIBSSL_SHA256}  /tmp/libssl.deb" | sha256sum -c - \
    && dpkg -i /tmp/libssl.deb && rm /tmp/libssl.deb

RUN curl -fsSL -o /tmp/oo.tar.gz \
        https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz \
    && echo "${OPENORBIS_SHA256}  /tmp/oo.tar.gz" | sha256sum -c - \
    && mkdir -p /opt && tar xzf /tmp/oo.tar.gz -C /opt && rm /tmp/oo.tar.gz

ENV OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain \
    DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 \
    HOME=/tmp
WORKDIR /src
