FROM --platform=linux/amd64 docker.io/archlinux:latest

RUN pacman -Syu --noconfirm \
    mosquitto \
    postgresql-libs \
    libmodbus \
    base-devel \
    pkgconf \
    git

RUN useradd -u 2000 -m subscriber

WORKDIR /app

COPY --chown=subscriber:subscriber . .
RUN rm -rf bin/ && chmod +x ./build.sh && ./build.sh build

CMD ["./bin/subscriber"]