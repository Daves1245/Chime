#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <defs.h>
#include <client.h>
#include <server.h>

void usage(char *binname) {
    printf("usage: %s [OPTION] (address)", binname);
    puts("instant messenger");
    puts("OPTIONS:");
    printf("up:\t\t-start a server (if a port is not given, the default is %s)", PORT);
}

void transfer_to_server(char *port, char *ft_port) {
    server_main(port, ft_port);
    exit(EXIT_SUCCESS);
}

void transfer_to_client(char *address, char *port, char *ft_port) {
    client_main(address, port, ft_port);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
    }

    if (strcmp(argv[1], "up") == 0) {
        char *port = PORT, *ft_port = FT_PORT;
        if (argc >= 2) {
            // TODO check validity of ip address
            // TODO handle both ipv4, ipv6
            printf("Address: %s\n", argv[2]);
            if (argc >= 3) {
                port = argv[3];
                if (argc >= 4) {
                    ft_port = argv[4];
                }
            }
        }

        transfer_to_server(port, ft_port);
    }

    if (strcmp(argv[1], "in") == 0) {
        if (argc < 3) {
            usage(argv[0]);
        }
        printf("Address: %s\n", argv[3]);
        // TODO check if port included in address is already handled
        char *address = LOCALHOST, *port = PORT, *ft_port = FT_PORT;
        if (argc > 3) {
            port = argv[4];
            if (argc > 4) {
                ft_port = argv[5];
            }
        }
        transfer_to_client(address, port, ft_port);
    }

    return 0;
}
