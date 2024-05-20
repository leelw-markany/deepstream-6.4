#!/bin/bash
SCRIPT=`realpath -s $0`
SCRIPTPATH=`dirname ${SCRIPT}`
SCRIPTNAME=`basename ${SCRIPT}`
cd ${SCRIPTPATH}
set -euo pipefail

# NVIDIA TAO ReIdentificationNet
# https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_plugin_gst-nvtracker.html#setup-sample-re-id-models
wget 'https://api.ngc.nvidia.com/v2/models/nvidia/tao/reidentificationnet/versions/deployable_v1.0/files/resnet50_market1501.etlt' \
    -O ./Tracker/resnet50_market1501.etlt

# ONNX Model
wget 'https://vision.in.tum.de/webshare/u/seidensc/GHOST/ghost_reid.onnx' \
    -O ./Tracker/ghost_reid.onnx

# PeopleNet
#https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_plugin_gst-nvtracker.html#setup-sample-re-id-models
#https://catalog.ngc.nvidia.com/orgs/nvidia/teams/tao/models/peoplenet/files?version=deployable_quantized_v2.6.1
wget --content-disposition \
    https://api.ngc.nvidia.com/v2/models/nvidia/tao/peoplenet/versions/deployable_quantized_v2.6.1/zip \
    -O ./peoplenet/deployable_quantized_v2.6.1.zip
(
    cd ./peoplenet
    unzip -o deployable_quantized_v2.6.1.zip
)

# Action Recognition Net
#https://catalog.ngc.nvidia.com/orgs/nvidia/teams/tao/models/actionrecognitionnet/files?version=deployable_v1.0
wget --content-disposition \
    https://api.ngc.nvidia.com/v2/models/nvidia/tao/actionrecognitionnet/versions/deployable_v1.0/zip \
    -O ./actionrecognitionnet/actionrecognitionnet_deployable_v1.0.zip
(
    cd ./actionrecognitionnet
    unzip -o actionrecognitionnet_deployable_v1.0.zip
)

# BodyPose3DNet
#https://catalog.ngc.nvidia.com/orgs/nvidia/teams/tao/models/bodypose3dnet/files?version=deployable_accuracy_v1.0
wget --content-disposition \
    https://api.ngc.nvidia.com/v2/models/nvidia/tao/bodypose3dnet/versions/deployable_accuracy_v1.0/zip \
    -O ./bodypose3dnet/deployable_accuracy_v1.0.zip
(
    cd ./bodypose3dnet
    unzip -o deployable_accuracy_v1.0.zip
)