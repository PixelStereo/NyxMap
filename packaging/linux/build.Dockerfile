# Release ABI baseline. The GitHub host and separate latest-Qt tests stay latest.
FROM ubuntu:24.04
RUN --mount=type=secret,id=ubuntu-ca,required=true,mode=0444 \
    printf 'Acquire::Retries "2";\nAcquire::https::Timeout "30";\nAcquire::https::No-Cache "true";\nAcquire::https::CaInfo "/run/secrets/ubuntu-ca";\n' > /etc/apt/apt.conf.d/80nyxmap-ci-build \
    && sed -i 's|http://archive.ubuntu.com/ubuntu|https://archive.ubuntu.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|https://security.ubuntu.com/ubuntu|g' /etc/apt/sources.list.d/ubuntu.sources \
    && sed 's/^Types:.*/Types: deb-src/' /etc/apt/sources.list.d/ubuntu.sources > /etc/apt/sources.list.d/nyxmap-source.sources \
    && { attempt=1; until apt-get -o APT::Update::Error-Mode=any update \
      && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git curl ca-certificates file python3 python3-pil clang nasm xvfb xauth libgl1-mesa-dri \
      qt6-base-dev libqt6opengl6-dev qt6-multimedia-dev qt6-svg-dev qt6-webengine-dev \
      qt6-wayland qt6-image-formats-plugins libqt6webenginecore6-bin libqt6webengine6-data libgl1-mesa-dev libasound2-dev libasound2-plugins \
      libavahi-client3 libpulse0 libssl-dev libsrt-openssl-dev libdav1d-dev libva-dev libdrm-dev \
      libtiff-dev libjpeg-dev zlib1g-dev liblzma-dev libdeflate-dev libzstd-dev libwebp-dev liblerc-dev \
      gstreamer1.0-plugins-base gstreamer1.0-plugins-good fonts-dejavu-core \
      ; do \
        if [ "$attempt" -ge 3 ]; then exit 100; fi; \
        echo "APT failed; refreshing package indexes (attempt $((attempt + 1))/3)" >&2; \
        rm -rf /var/lib/apt/lists/*; sleep "$((attempt * 5))"; attempt=$((attempt + 1)); \
      done; } \
    && file --version \
    && rm /etc/apt/apt.conf.d/80nyxmap-ci-build
ARG BUILDER_UID=1001
ARG BUILDER_GID=1001
RUN groupadd --force --gid ${BUILDER_GID} nyxmap && useradd --create-home --uid ${BUILDER_UID} --gid ${BUILDER_GID} nyxmap
USER nyxmap
ENV DOTNET_ROOT=/dotnet
ENV PATH="/dotnet:${PATH}"
ENV PYTHONUNBUFFERED=1
WORKDIR /workspace
