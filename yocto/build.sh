#!/usr/bin/env bash
#
# Build the Mihini data-store image for the Raspberry Pi 3B inside a crops/poky
# container, then copy the deploy artifacts to a host directory.
#
# Prerequisites: Docker, and ~20 GB free disk (the first build is slow).
#
# Env overrides:
#   YOCTO_WORKSPACE  host directory for the layers + caches + artifacts
#                    (default: ~/yocto-rpi3)
#   IMAGE            target image (default: core-image-base)
#   DOCKER_IMAGE     build image (default: crops/poky:ubuntu-22.04)
#   MIHINI_REPO      path to this repository (default: parent of this script)
#
# The workspace is mounted into the container at /workdir and this repo at /repo,
# so the caches persist on the host and the finished artifacts land in
#   $YOCTO_WORKSPACE/deploy/raspberrypi3-64/
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${MIHINI_REPO:-$(cd "${SCRIPT_DIR}/.." && pwd)}"

WORKSPACE="${YOCTO_WORKSPACE:-$HOME/yocto-rpi3}"
IMAGE="${IMAGE:-core-image-base}"
DOCKER_IMAGE="${DOCKER_IMAGE:-crops/poky:ubuntu-22.04}"
BRANCH="scarthgap"

mkdir -p "${WORKSPACE}"
cd "${WORKSPACE}"

clone_if_missing() {
    local dir="$1" url="$2"
    if [ ! -d "${dir}/.git" ]; then
        echo ">> cloning ${url} (branch ${BRANCH})"
        git clone --depth 1 -b "${BRANCH}" "${url}" "${dir}"
    fi
}

clone_if_missing poky              https://github.com/yoctoproject/poky.git
clone_if_missing meta-openembedded https://github.com/openembedded/meta-openembedded.git
clone_if_missing meta-raspberrypi  https://github.com/agherzan/meta-raspberrypi.git

mkdir -p downloads sstate-cache build deploy

echo ">> workspace : ${WORKSPACE}"
echo ">> repo      : ${REPO_DIR}"
echo ">> image     : ${IMAGE}"

docker run --rm \
    -v "${WORKSPACE}:/workdir" \
    -v "${REPO_DIR}:/repo" \
    "${DOCKER_IMAGE}" \
    --workdir=/workdir \
    bash -c "set -euo pipefail
        install -d /workdir/build/conf
        cp /repo/yocto/conf/local.conf.sample    /workdir/build/conf/local.conf
        cp /repo/yocto/conf/bblayers.conf.sample /workdir/build/conf/bblayers.conf
        . /workdir/poky/oe-init-build-env /workdir/build
        bitbake ${IMAGE}
        mkdir -p /workdir/deploy
        rm -rf /workdir/deploy/raspberrypi3-64
        cp -r /workdir/build/tmp/deploy/images/raspberrypi3-64 /workdir/deploy/
    "

echo
echo ">> done. Artifacts on the host:"
echo "   ${WORKSPACE}/deploy/raspberrypi3-64/"
ls -1 "${WORKSPACE}/deploy/raspberrypi3-64" 2>/dev/null | head || true
