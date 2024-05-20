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


# Install Apache Kafka
RUN apt install -y openjdk-8-jdk
#export JAVA_HOME=$(readlink -f /usr/bin/java | sed "s:bin/java::")
ENV JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64/jre/

RUN mkdir /opt/apache/
RUN wget 'https://downloads.apache.org/kafka/3.6.1/kafka_2.13-3.6.1.tgz' -P /opt/apache/

WORKDIR /opt/apache/
RUN tar -zxf kafka_2.13-3.6.1.tgz
RUN ln -s ./kafka_2.13-3.6.1 ./kafka
#WORKDIR /opt/apache/kafka

ENV PATH=/opt/apache/kafka/bin/:$PATH

# Install supervisord
RUN apt-get update && apt-get install -y supervisor
COPY supervisord-kafka.conf /etc/supervisor/conf.d/supervisord.conf
CMD ["/usr/bin/supervisord", "-c", "/etc/supervisor/conf.d/supervisord.conf"]

ENV CUDA_VER=12
WORKDIR /opt/nvidia/deepstream/deepstream/sources/apps
