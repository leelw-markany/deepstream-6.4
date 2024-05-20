#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

IMAGE=deepstream:6.4
SOURCE=/opt/nvidia/deepstream/deepstream-6.4/sources/apps
DESTINATION=./apps

container=$(docker create $IMAGE)
sudo rm -rf ./$DESTINATION
docker cp $container:$SOURCE $DESTINATION
docker rm $container

#git checkout apps/apps-common/src/.gitignore
#git checkout apps/sample_apps/deepstream-app/.gitignore

#git checkout apps/sample_apps/deepstream-app/run.sh
#git checkout apps/sample_apps/deepstream-app/cfg-kafka.txt
#git checkout apps/sample_apps/deepstream-app/config-msgconv.yml
#git checkout apps/sample_apps/deepstream-app/config.yml
#git checkout apps/sample_apps/deepstream-app/sources_4.csv
#git checkout apps/sample_apps/deepstream-app/sources_rtsp.csv

exit $?

