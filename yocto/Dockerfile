# Optional custom build image for the containerized Yocto build.
#
# build.sh uses crops/poky directly by default (set DOCKER_IMAGE to use a custom
# image). This file shows how to extend it -- e.g. to add tools for flashing the
# produced SD-card image. Build it with:
#
#     docker build -t mihini-yocto .
#
# then run build.sh with: DOCKER_IMAGE=mihini-yocto ./build.sh

FROM crops/poky:ubuntu-22.04

USER root
RUN apt-get update \
    && apt-get install -y --no-install-recommends kpartx bmap-tools \
    && rm -rf /var/lib/apt/lists/*
USER usersetup
