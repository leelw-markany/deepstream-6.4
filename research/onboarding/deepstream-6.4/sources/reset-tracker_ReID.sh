#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

IMAGE=deepstream:6.4
SOURCE=/opt/nvidia/deepstream/deepstream-6.4/sources/tracker_ReID
DESTINATION=./tracker_ReID

container=$(docker create $IMAGE)
sudo rm -rf ./$DESTINATION
docker cp $container:$SOURCE $DESTINATION
docker rm $container

exit $?

