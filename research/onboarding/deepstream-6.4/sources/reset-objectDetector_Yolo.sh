#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

IMAGE=deepstream:6.4
SOURCE=/opt/nvidia/deepstream/deepstream-6.4/sources/objectDetector_Yolo
DESTINATION=./objectDetector_Yolo

container=$(docker create $IMAGE)
sudo rm -rf ./$DESTINATION
docker cp $container:$SOURCE $DESTINATION
docker rm $container

#git checkout objectDetector_Yolo/.gitignore

#git checkout objectDetector_Yolo/nvdsinfer_custom_impl_Yolo/.gitignore
#git checkout objectDetector_Yolo/deepstream_app_config_yoloV3.txt

exit $?

