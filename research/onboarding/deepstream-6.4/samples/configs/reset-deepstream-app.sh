#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

IMAGE=deepstream:6.4
SOURCE=/opt/nvidia/deepstream/deepstream-6.4/samples/configs/deepstream-app
DESTINATION=./deepstream-app

container=$(docker create $IMAGE)
sudo rm -rf ./$DESTINATION
docker cp $container:$SOURCE $DESTINATION
docker rm $container

#git checkout deepstream-app/config_infer_primary_PeopleNet.txt
#git checkout deepstream-app/config_tracker_NvDCF_accuracy.yml
#git checkout deepstream-app/config_tracker_NvDeepSORT.yml

exit $?

