#ifndef CONFIG_H
#define CONFIG_H

// Set SERVER_HOST at build time:
//   docker compose run --rm -e SERVER_HOST=192.168.1.50 3ds-build
// Or find your IP with: ifconfig | grep "inet " | grep -v 127.0.0.1
#ifndef SERVER_HOST
#define SERVER_HOST "192.168.1.100"
#endif

#define SERVER_PORT 3333

#endif // CONFIG_H
