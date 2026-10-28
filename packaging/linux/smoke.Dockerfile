ARG UBUNTU_VERSION=24.04
FROM ubuntu:${UBUNTU_VERSION}
# Only a test harness and the host graphics/display stack. No application libraries.
# The minimal base has no CA package yet. Use the runner's public CA bundle
# for this build step only, so every package download can use verified HTTPS.
# docker build --secret id=ubuntu-ca,src=/etc/ssl/certs/ca-certificates.crt ...
RUN --mount=type=secret,id=ubuntu-ca,required=true,mode=0444 \
    printf 'Acquire::Retries "2";\nAcquire::http::Timeout "30";\nAcquire::https::Timeout "30";\nAcquire::https::CaInfo "/run/secrets/ubuntu-ca";\n' \
        > /etc/apt/apt.conf.d/80nyxmap-ci-build \
    && sed -i 's|http://archive.ubuntu.com/ubuntu|https://archive.ubuntu.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|https://security.ubuntu.com/ubuntu|g' \
        /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3 xvfb xauth libgl1-mesa-dri libglx-mesa0 libegl1 libegl-mesa0 libgles2 libopengl0 \
    && rm -rf /var/lib/apt/lists/* && rm /etc/apt/apt.conf.d/80nyxmap-ci-build
RUN useradd --create-home nyxmap
USER nyxmap
ENV LIBGL_ALWAYS_SOFTWARE=1
ENV PYTHONUNBUFFERED=1
WORKDIR /tmp
