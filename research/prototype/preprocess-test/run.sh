#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}

set -euo pipefail
(
	cd ../../../gst-plugins/_gst-nvdspreprocess/nvdspreprocess_lib/
	make && make install
	cd ..
	make && make install
)

make
# ./deepstream-preprocess-test rtsp://admin:alchera1@192.168.1.45/ISAPI/Streaming/channels/201 rtsp://admin:alchera1@192.168.1.45/ISAPI/Streaming/channels/501 rtsp://admin:alchera1@192.168.1.45/ISAPI/Streaming/channels/801 rtsp://admin:alchera1@192.168.1.45/ISAPI/Streaming/channels/1001

./deepstream-preprocess-test rtsp://admin:alchera1@192.168.1.45/ISAPI/Streaming/channels/801
