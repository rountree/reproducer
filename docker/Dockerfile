FROM fluxrm/flux-sched:noble-v0.48.0-amd64

# Run as root for package installation
USER root

# Install build dependencies
# Note: flux-core and dev headers already in base image
RUN apt-get update && \
    apt-get install -y \
        build-essential \
        pkg-config \
        gdb \
        sudo && \
    rm -rf /var/lib/apt/lists/*

# Create fluxuser
RUN useradd -ms /bin/bash fluxuser && \
    echo "fluxuser ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Copy reproducer source
COPY flux-event-watch-test.c /home/fluxuser/
COPY Makefile /home/fluxuser/
COPY run_test.sh /home/fluxuser/
RUN chmod +x /home/fluxuser/run_test.sh

# Build and install the plugin
WORKDIR /home/fluxuser
RUN make && \
    mkdir -p /usr/lib/flux/shell && \
    cp flux-event-watch-test.so /usr/lib/flux/shell/ && \
    chown -R fluxuser:fluxuser /home/fluxuser

USER fluxuser
WORKDIR /home/fluxuser

CMD ["/bin/bash"]
