FROM ubuntu:18.04

ENV PROGRAM_ARGS="5 4 100 100"

RUN apt-get update \
  && apt-get install -y ssh \
    build-essential \
    gcc \
    gdb \
    clang \
    cmake \
    rsync \
    less \
  && apt-get clean

RUN echo "alias proj='cd /tmp/proj2; l'" >> /root/.bashrc
RUN echo "alias chck='less /tmp/proj2/proj2.out'" >> /root/.bashrc
RUN echo "alias test='cd /tmp/proj2 && make ; ./proj2 $PROGRAM_ARGS'" >> /root/.bashrc

RUN mkdir /tmp/proj2
