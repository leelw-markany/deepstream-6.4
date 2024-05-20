#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

rm -rf ./reid-track-output
mkdir reid-track-output
./deepstream-app -c ./_configs/_config-kafka.yml -t

exit $?