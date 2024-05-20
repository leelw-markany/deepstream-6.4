#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

export DISPLAY=:1
xhost +
#docker rm -f deepstream64-devel
sudo rm -rf /tmp/.X11-unix

docker run -it \
  --rm \
  --gpus "device=0" \
  --net=host \
  --name=deepstream64-devel \
  -v /tmp/.X11-unix/:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  -e CUDA_CACHE_DISABLE=0 \
  -e CUDA_VER=12 \
  --device /dev/snd \
  nvcr.io/nvidia/deepstream:6.4-gc-triton-devel

exit $?

# cd /opt/nvidia/deepstream/deepstream-6.4/sources/apps/sample_apps/deepstream-test1
# make
# ./deepstream-test1-app dstest1_config.yml