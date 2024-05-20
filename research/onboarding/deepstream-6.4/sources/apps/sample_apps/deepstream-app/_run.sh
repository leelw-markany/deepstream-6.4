#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

rm -rf ./out.mp4
rm -rf ./reid-track-output
mkdir reid-track-output
./deepstream-app -c ./_configs/_config.yml -t

exit $?