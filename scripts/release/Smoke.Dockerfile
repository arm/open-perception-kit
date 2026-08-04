FROM debian:trixie-slim@sha256:3a39a0592364683e6bab97937b72cad5a8fa6dcbbee90edb3bb48c7f8e94f258

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        binutils python3-gst-1.0 \
        libssl3t64 libsoup-3.0-0 libjson-glib-1.0-0 libcairo2 libusb-1.0-0 \
        libgstreamer1.0-0 \
        gstreamer1.0-plugins-base gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-good gstreamer1.0-nice && \
    rm -rf /var/lib/apt/lists/* && \
    useradd --create-home --uid 10001 pek

USER pek
