FROM nvcr.io/nvidia/deepstream:6.4-gc-triton-devel

########
# Install necessary packages for building GLib (According to release notes : https://github.com/GNOME/glib/blob/2.76.6/INSTALL.md)
RUN apt update -y
RUN apt install -y pkg-config python3-dev libffi-dev libmount-dev meson ninja-build wget libxml2-utils

# Install GLIB 2.76.6
WORKDIR /opt/nvidia/deepstream/deepstream/lib
RUN git clone -b 2.76.6 https://github.com/GNOME/glib.git ./glibc-2.76.6 

WORKDIR /opt/nvidia/deepstream/deepstream/lib/glibc-2.76.6
RUN git submodule update --init --recursive

RUN meson _build 
RUN ninja -C _build
RUN ninja -C _build install

ENV LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu/:$LD_LIBRARY_PATH

########
#RUN mkdir /opt/nvidia/deepstream/deepstream/samples/models/Tracker/ 
#RUN wget 'https://api.ngc.nvidia.com/v2/models/nvidia/tao/reidentificationnet/versions/deployable_v1.0/files/resnet50_market1501.etlt' -P /opt/nvidia/deepstream/deepstream/samples/models/Tracker/
#RUN wget 'https://vision.in.tum.de/webshare/u/seidensc/GHOST/ghost_reid.onnx' -P /opt/nvidia/deepstream/deepstream/samples/models/Tracker/
ENV CUDA_VER=12
WORKDIR /opt/nvidia/deepstream/deepstream/sources/apps
