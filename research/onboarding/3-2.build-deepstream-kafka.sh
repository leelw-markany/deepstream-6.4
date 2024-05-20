#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

sudo docker build \
    -f deepstream-kafka.dockerfile \
    -t deepstream-kafka:6.4 \
    .

exit $?
