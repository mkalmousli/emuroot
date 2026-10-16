# Test image: emuroot built from source, plus the root filesystems of several
# real distributions to run inside it. Build context is the repository root.
#
#   docker build -t emuroot-test -f tests/docker/Dockerfile .
#   docker run --rm --cap-add SYS_PTRACE --user 1000 emuroot-test
#
# --cap-add SYS_PTRACE is needed because Docker's default seccomp profile only
# allows process_vm_readv/writev (which emuroot uses) with that capability.

FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends gcc make libc6-dev gcc-multilib \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile ./
COPY include include
COPY src src
COPY tests/embed_test.c tests/embed_test.c
RUN make OUT=/out/native && make OUT=/out/native embed_test && make PTRACE=0 STATIC=0 OUT=/out/portable \
    && make CC="gcc -m32" OUT=/out/x86


# 32-bit x86: the kernel runs i386 userlands natively, so the 32-bit build is
# tested against an i386 root filesystem.
FROM --platform=linux/386 i386/alpine:3.20 AS i386-alpine


FROM ubuntu:24.04 AS test
RUN apt-get update && apt-get install -y --no-install-recommends python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
# An unprivileged user, like Android's `shell`: emuroot must work without root.
RUN useradd --uid 2000 --create-home tester

# Distribution root filesystems under test, owned by the test user.
COPY --chown=2000:2000 --from=ubuntu:24.04   / /rootfs/ubuntu-24.04/
COPY --chown=2000:2000 --from=ubuntu:22.04   / /rootfs/ubuntu-22.04/
COPY --chown=2000:2000 --from=debian:12      / /rootfs/debian-12/
COPY --chown=2000:2000 --from=alpine:3.20    / /rootfs/alpine-3.20/
COPY --chown=2000:2000 --from=fedora:40      / /rootfs/fedora-40/
COPY --chown=2000:2000 --from=busybox:glibc  / /rootfs/busybox/
COPY --chown=2000:2000 --from=i386-alpine    / /rootfs/i386-alpine-3.20/

COPY --from=build /out /opt/emuroot/build
COPY test.py /opt/emuroot/test.py
COPY tests /opt/emuroot/tests
WORKDIR /opt/emuroot
USER tester
ENTRYPOINT ["python3", "test.py", "--local", "--rootfs", "/rootfs"]
