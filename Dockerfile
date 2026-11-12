FROM alpine:3 AS builder

RUN apk update && apk add \
	musl-dev \
	gcc \
	&& rm -rf /var/cache/apk/*

# Copy all files for building the project.
WORKDIR /app
COPY . .

# Compile the server application.
RUN gcc -ansi -std=gnu89 -Wall -pedantic -pthread amigos.c -o amigos

FROM alpine:3

# Copy files for deployment.
WORKDIR /app
COPY ./filetypes.conf .
COPY --from=builder /app/amigos .

EXPOSE 70
ENTRYPOINT /app/amigos $GOPHER_HOSTNAME 70 /docroot
