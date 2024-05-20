#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

export DISPLAY=:1
xhost +
#docker rm -f deepstream-kafka
sudo rm -rf /tmp/.X11-unix

docker run -d \
  --gpus all \
  --net=host \
  --name=deepstream-kafka \
  -v /tmp/.X11-unix/:/tmp/.X11-unix \
  -v ./deepstream-6.4/samples/models/Tracker/:/opt/nvidia/deepstream/deepstream-6.4/samples/models/Tracker \
  -v ./deepstream-6.4/samples/models/peoplenet/:/opt/nvidia/deepstream/deepstream-6.4/samples/models/peoplenet \
  -v ./deepstream-6.4/samples/models/actionrecognitionnet/:/opt/nvidia/deepstream/deepstream-6.4/samples/models/actionrecognitionnet \
  -v ./deepstream-6.4/samples/models/bodypose3dnet/:/opt/nvidia/deepstream/deepstream-6.4/samples/models/bodypose3dnet \
  -v ./deepstream_reference_apps/:/opt/nvidia/deepstream/deepstream-6.4/sources/apps/deepstream_reference_apps \
  -e DISPLAY=$DISPLAY \
  -e CUDA_CACHE_DISABLE=0 \
  --device /dev/snd \
  deepstream-kafka:6.4

docker exec -it deepstream-kafka /bin/bash

docker rm deepstream-kafka

exit $?

# cd /opt/nvidia/deepstream/deepstream-6.4/sources/apps/sample_apps/deepstream-test1
# make
# ./deepstream-test1-app dstest1_config.yml
