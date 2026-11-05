# Base environment
FROM ubuntu:22.04

# Environment settings
ENV DEBIAN_FRONTEND=noninteractive \
    WORKSPACE=/app

WORKDIR ${WORKSPACE}

# Install essential dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    python3 \
    python3-pip \
    nodejs \
    npm \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy repository contents
COPY . ${WORKSPACE}

# Build artifacts (POSIX fallback build if present)
RUN mkdir -p build && cd build && (cmake .. && make -j$(nproc) || true)

# Expose Cockpit Mission Control Port
EXPOSE 3080

# Entry point
CMD ["bash", "-c", "echo '🚀 QNX-Cortex-Cage Environment Ready!' && bash"]
