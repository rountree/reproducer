FROM fluxrm/flux-sched@sha256:efd2a45e7b0797140ac33ef62b75f1324e3db195628f9d4a5fbea35098092d7b

USER root

RUN apt-get update && \
    apt-get install -y \
        build-essential \
        pkg-config && \
    rm -rf /var/lib/apt/lists/*

RUN echo "fluxuser ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Copy test files
COPY test-plugin.c /home/fluxuser/
COPY test.rc /home/fluxuser/
COPY Makefile /home/fluxuser/
COPY run_test.sh /home/fluxuser/
RUN chmod +x /home/fluxuser/run_test.sh

# Build and install the plugin
WORKDIR /home/fluxuser
RUN make && \
    mkdir -p /usr/lib/flux/shell && \
    cp test-plugin.so /usr/lib/flux/shell/ && \
    chown -R fluxuser:fluxuser /home/fluxuser

USER fluxuser
WORKDIR /home/fluxuser

CMD ["/bin/bash"]
