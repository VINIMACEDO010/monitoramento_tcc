FROM docker.io/archlinux:latest

RUN pacman -Syu --noconfirm mosquitto postgresql libmodbus base-devel git

RUN useradd -u 2000 -m subscriber

WORKDIR /app

COPY --chown=subscriber:subscriber . .
RUN rm -rf bin/ ; chmod +x ./build.sh && ./build.sh build && ls

CMD ["./bin/subscriber"]
