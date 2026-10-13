#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

// max number of peers
#define MAX_PEERS 50

// PDU struct definition
struct pdu {
    char type;
    char data[100];
};

// store peer registration info: name, content, ip, and port
struct registration {
    char peer_name[20];
    char content_name[20];
    char ip[20];
    int port;
};


int main(int argc, char *argv[]) {
    // setup UDP socket, server, and client structs, two PDUs for
    // receiving, sending mesages, table for registations, and counter
    // for how many peers are registerd
    int sd, port;
    struct sockaddr_in server, client;
    socklen_t len = sizeof(client);
    struct pdu recv_pdu, send_pdu;
    struct registration reg_table[MAX_PEERS];
    int reg_count = 0;

    // if incorrect number of arguments given, exit
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // get server port number, create UDP socket, stop program if socket
    // cannot be created
    port = atoi(argv[1]);
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) { perror("socket"); exit(1); }

    // prepare server address settings with port and IP
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    // bind UDP socket to server address to start receiving messages
    // stop program if binding fails
    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind");
        exit(1);
    }

    // indicate port server is running on
    printf("Index server running on port %d...\n", port);

    while (1) {
        // listen for PDU, if receiving fails, print error, keep waiting
        if (recvfrom(sd, &recv_pdu, sizeof(recv_pdu), 0, (struct sockaddr *)&client, &len) < 0) {
            perror("recvfrom");
            continue;
        }

        // PDU successfully received message
        printf("Received PDU type: %c\n", recv_pdu.type);

        
        if (recv_pdu.type == 'R') {
            // reject registration if the table is full
            if (reg_count >= MAX_PEERS) {
                send_pdu.type = 'E';
                sprintf(send_pdu.data, "Registration table full");
                sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&client, len);
                continue;
            }

            // store peer registration info from PDU, store in registration table
            struct registration r;
            sscanf(recv_pdu.data, "%s %s %s %d", r.peer_name, r.content_name, r.ip, &r.port);
            reg_table[reg_count++] = r;

            // A-type acknowledgement PDU to indicate successful registration
            send_pdu.type = 'A';
            sprintf(send_pdu.data, "Registered %s (%s)", r.peer_name, r.content_name);
            sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&client, len);
            printf("Registered: %s %s %s:%d\n", r.peer_name, r.content_name, r.ip, r.port);

        // S-type PDU, search registration table for match, if match, send back S-type
        // PDU with matching peer's IP, name, port
        } else if (recv_pdu.type == 'S') {
            char content[20];
            sscanf(recv_pdu.data, "%s", content);
            int found = 0;
            int i;
            for (i = 0; i < reg_count; i++) {
                if (strcmp(reg_table[i].content_name, content) == 0) {
                    send_pdu.type = 'S';
                    sprintf(send_pdu.data, "%s %s %d", reg_table[i].ip, reg_table[i].peer_name, reg_table[i].port);
                    sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&client, len);
                    found = 1;
                    break;
                }
            }
            // if not found, send E-type (error) PDU to peer saying content does not exist
            if (!found) {
                send_pdu.type = 'E';
                sprintf(send_pdu.data, "Content not found");
                sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&client, len);
            }

        // list request: build text list of registered content and peer names
        } else if (recv_pdu.type == 'O') {
            send_pdu.type = 'O';
            char buf[1000] = "";
            int i;
            for (i = 0; i < reg_count; i++) {
                char line[80];
                sprintf(line, "%s (%s)\n", reg_table[i].content_name, reg_table[i].peer_name);
                strcat(buf, line);
            }
            // send list to PDU data field, and back to peer
            strncpy(send_pdu.data, buf, sizeof(send_pdu.data) - 1);
            send_pdu.data[sizeof(send_pdu.data) - 1] = 0;
            sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&client, len);

        // deregister request: read peer name, find peer in registration table,
        // remove it, shift other entries up in table
        } else if (recv_pdu.type == 'T') {
            // Deregister
            char name[20];
            sscanf(recv_pdu.data, "%s", name);
            int i;
            // remove every entry this peer registered
            for (i = 0; i < reg_count; ) {
                if (strcmp(reg_table[i].peer_name, name) == 0) {
                    int j;
                    for (j = i; j < reg_count - 1; j++) reg_table[j] = reg_table[j + 1];
                    reg_count--;
                } else {
                    i++;
                }
            }
            // acknowledge that peer successfully deregistered
            send_pdu.type = 'A';
            sprintf(send_pdu.data, "Deregistered %s", name);
            sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&client, len);
        }
    }

    // close socket, exit program
    close(sd);
    return 0;
}



