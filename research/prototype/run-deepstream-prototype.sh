#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail
set -x

CONTAINER_NAME="deepstream-prototype"
if [ "$(docker ps -qa -f name=${CONTAINER_NAME})" ]; then
    echo "컨테이너가 실행 중입니다. 종료 및 삭제를 시도합니다."
    docker stop ${CONTAINER_NAME}
    docker rm -f ${CONTAINER_NAME}
fi

export DISPLAY=:1
xhost +
sudo rm -rf /tmp/.X11-unix

docker run -d \
  --gpus all \
  --net=host \
  --name=${CONTAINER_NAME} \
  -v /tmp/.X11-unix/:/tmp/.X11-unix \
  -v ./models/Tracker/:/opt/nvidia/deepstream/deepstream-6.4/prototype/models/Tracker \
  -v ./models/peoplenet/:/opt/nvidia/deepstream/deepstream-6.4/prototype/models/peoplenet \
  -v ./models/actionrecognitionnet/:/opt/nvidia/deepstream/deepstream-6.4/prototype/models/actionrecognitionnet \
  -v ./models/bodypose3dnet/:/opt/nvidia/deepstream/deepstream-6.4/prototype/models/bodypose3dnet \
  -v ./includes/:/opt/nvidia/deepstream/deepstream-6.4/sources/includes \
  -v ./apps-common/:/opt/nvidia/deepstream/deepstream-6.4/sources/apps/apps-common \
  -v ./src/:/opt/nvidia/deepstream/deepstream-6.4/sources/prototype \
  -v ./preprocess-test/:/opt/nvidia/deepstream/deepstream-6.4/sources/apps/sample_apps/_preprocess-test \
  -v ./gst-nvdspreprocess/:/opt/nvidia/deepstream/deepstream-6.4/sources/gst-plugins/_gst-nvdspreprocess \
  -e DISPLAY=$DISPLAY \
  -e CUDA_CACHE_DISABLE=0 \
  --device /dev/snd \
  ${CONTAINER_NAME}:6.4

docker exec -it ${CONTAINER_NAME} /bin/bash

docker stop ${CONTAINER_NAME}
docker rm ${CONTAINER_NAME}

exit $?
