FROM fluxrm/flux-sched@sha256:efd2a45e7b0797140ac33ef62b75f1324e3db195628f9d4a5fbea35098092d7b

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

# Add sudo permissions for fluxuser (already exists in base image)
RUN echo "fluxuser ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Copy flux configuration
COPY flux-config/ /home/fluxuser/flux-config/

# Copy reproducer source
COPY flux-event-watch-test.c /home/fluxuser/
COPY Makefile /home/fluxuser/
COPY run_test.sh /home/fluxuser/
COPY docker/entrypoint.sh /home/fluxuser/
RUN chmod +x /home/fluxuser/run_test.sh /home/fluxuser/entrypoint.sh

# Build and install the plugin
WORKDIR /home/fluxuser
RUN make && \
    mkdir -p /usr/lib/flux/shell && \
    cp flux-event-watch-test.so /usr/lib/flux/shell/ && \
    chown -R fluxuser:fluxuser /home/fluxuser

USER fluxuser
WORKDIR /home/fluxuser

ENTRYPOINT ["/home/fluxuser/entrypoint.sh"]
