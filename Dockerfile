# ---- Build stage -----------------------------------------------------------
FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY Makefile ./
COPY src ./src
COPY tests ./tests

# ARCH selects the CPU target. Default is a portable baseline so the image runs on
# any machine of that architecture; pass --build-arg ARCH=native for maximum speed.
ARG ARCH=""
# The build also runs the perft suite: the image is refused if move generation is wrong.
RUN if [ -z "$ARCH" ]; then \
      case "$(uname -m)" in \
        x86_64)  ARCH=x86-64-v2 ;; \
        aarch64) ARCH=armv8-a ;; \
        *)       ARCH= ;; \
      esac; \
    fi \
 && make -j"$(nproc)" ARCH="$ARCH" STATIC=1 \
 && ./engine perftsuite tests/perft_suite.epd 4

# ---- Runtime stage ---------------------------------------------------------
FROM debian:bookworm-slim
RUN useradd --system --no-create-home chess
COPY --from=build /src/engine /usr/local/bin/engine
USER chess

ENV HOST=0.0.0.0 \
    PORT=8080 \
    THREADS=2 \
    HASH=128
EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/engine"]
CMD ["serve"]
