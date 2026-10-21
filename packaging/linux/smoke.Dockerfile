ARG UBUNTU_VERSION=24.04
FROM ubuntu:${UBUNTU_VERSION}
# Only a test harness and the host graphics/display stack. No application libraries.
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 xvfb xauth libgl1-mesa-dri libglx-mesa0 \
    && rm -rf /var/lib/apt/lists/*
RUN useradd --create-home pinol
USER pinol
ENV LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /tmp
