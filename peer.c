#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUF_SIZE 100

// represents PDU structure
struct pdu {
    char type;
    char data[100];
};

// setup file server
void *tcp_server(void *arg) {
    // made up of port nmber, socket descriptors, and address
    int port = *(int *)arg;
    int sd, new_sd;
    struct sockaddr_in server, client;
    socklen_t len = sizeof(client);
    char buf[BUF_SIZE]; // buffer for sending file data
    // BUF_SIZE represents size of the data being sent
    FILE *fp; // file pointer for opening requested file

    // creates tcp socket using server's address info
    sd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1; // allow restarting on the same port right away
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    // bind socket to server address, stop if the port is unavailable
    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("[TCP Server] bind");
        exit(1);
    }
    listen(sd, 5); // accept incoming connections from peers

    // display waiting for incoming connections
    printf("[TCP Server] Listening on port %d...\n", port);

    
    while (1) {
        // waiting to accept peer to connect
        new_sd = accept(sd, (struct sockaddr *)&client, &len);
        if (new_sd < 0) continue; // if connection fails, continue waiting

        // receive filename from peer and null-terminate it
        char filename[BUF_SIZE];
        int got = read(new_sd, filename, sizeof(filename) - 1);
        filename[got > 0 ? got : 0] = 0;
        filename[strcspn(filename, "\n")] = 0;

        // only serve files from this folder (no paths)
        fp = (strchr(filename, '/') || strstr(filename, "..")) ? NULL : fopen(filename, "rb");
        if (!fp) { // if file does not exist, send error message
            write(new_sd, "ERROR", 6);
            close(new_sd);
            continue;
        }
        
        // write file data into buffer, and send to peer
        int n;
        while ((n = fread(buf, 1, BUF_SIZE, fp)) > 0)
            write(new_sd, buf, n);

        // close file and socket, give confirmation message
        fclose(fp);
        close(new_sd);
        printf("[TCP Server] Sent file %s\n", filename);
    }
}

int main(int argc, char *argv[]) {
    // if user gives wrong number of arguments, exit
    if (argc != 4) {
        printf("Usage: %s <index_server_ip> <index_server_port> <peer_tcp_port>\n", argv[0]);
        exit(1);
    }

    // save command line arguments: index server IP, index server port, TCP port
    char *index_ip = argv[1];
    int index_port = atoi(argv[2]);
    int tcp_port = atoi(argv[3]);

    // start child thread for TCP server
    pthread_t tid;
    pthread_create(&tid, NULL, tcp_server, &tcp_port);

    // create UDP socket with index server IP and port
    // for peer to send PDU
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(index_port);
    inet_pton(AF_INET, index_ip, &server.sin_addr);

    // create two PDUs, one for sending and one for receiving message
    struct pdu send_pdu, recv_pdu;
    socklen_t len = sizeof(server); // store size fo server address
    char my_name[20] = ""; // peer name used when registering, needed to deregister on quit

    while (1) {
        // prompt user with choice of action
        printf("\n1. Register\n2. Search\n3. Download\n4. List\n5. Quit\n> ");
        int choice;
        scanf("%d", &choice);

        // choice 1: register file
        if (choice == 1) {
            // prompt peer for peer name and file name for registering content
            char peer[20], content[20];
            printf("Peer name: "); scanf("%s", peer);
            printf("Content name: "); scanf("%s", content);
            strcpy(my_name, peer);

            // fill in PDU information and send PDU to index server with UDP
            send_pdu.type = 'R';
            sprintf(send_pdu.data, "%s %s %s %d", peer, content, "127.0.0.1", tcp_port);
            sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&server, len);

            // wait for index server response PDU, print message in data field
            recvfrom(sd, &recv_pdu, sizeof(recv_pdu), 0, NULL, NULL);
            printf("Server: %s\n", recv_pdu.data);

        // choice 2: search option - searches for files based on file names
        } else if (choice == 2) {
            // prompt peer for file name
            char content[20];
            printf("Enter content name: "); scanf("%s", content);

            // create S-type PDU with given information, send to and wait for index server response
            send_pdu.type = 'S';
            sprintf(send_pdu.data, "%s", content);
            sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&server, len);
            recvfrom(sd, &recv_pdu, sizeof(recv_pdu), 0, NULL, NULL);

            // print message from index server
            printf("Response: %s\n", recv_pdu.data);

        // choice 3: download option - download and read file data
        } else if (choice == 3) {
            // prompt peer for file download information
            char ip[20], filename[30];
            int port;
            printf("Server IP: "); scanf("%s", ip);
            printf("Server Port: "); scanf("%d", &port);
            printf("File to download: "); scanf("%s", filename);

            // create TCP socket with content server IP and port for peer to connect to download file
            int tcp_sd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in peer;
            peer.sin_family = AF_INET;
            peer.sin_port = htons(port);
            inet_pton(AF_INET, ip, &peer.sin_addr);

            // connect to content server, if fail, print error and skip download
            if (connect(tcp_sd, (struct sockaddr *)&peer, sizeof(peer)) < 0) {
                perror("connect");
                continue;
            }

            // send filename to server using TCP connection, create new file to store contents,
            // read data from server until transfer ends then close everything and
            // give success message
            write(tcp_sd, filename, strlen(filename));
            int n;
            char buf[BUF_SIZE];

            // an "ERROR" reply means the other peer could not send the file
            n = read(tcp_sd, buf, BUF_SIZE);
            if (n <= 0 || (n == 6 && memcmp(buf, "ERROR", 6) == 0)) {
                close(tcp_sd);
                printf("Download failed: %s not available on that peer\n", filename);
                continue;
            }

            FILE *fp = fopen(filename, "wb");
            do {
                fwrite(buf, 1, n, fp);
            } while ((n = read(tcp_sd, buf, BUF_SIZE)) > 0);
            fclose(fp);
            close(tcp_sd);
            printf("Downloaded %s successfully\n", filename);

        // choice 4: list option - list all files and which peer each file is registered to
        } else if (choice == 4) {
            // send O-type PDU to index server, receive server reply, print all registered content
            send_pdu.type = 'O';
            strcpy(send_pdu.data, "list");
            sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&server, len);
            recvfrom(sd, &recv_pdu, sizeof(recv_pdu), 0, NULL, NULL);
            printf("Registered content:\n%s\n", recv_pdu.data);

        // choice 5: quit option - deregisters peer from the index server
        } else if (choice == 5) {
            // send T-PDU to deregister peer from index server, print goodbye, exit program
            send_pdu.type = 'T';
            strcpy(send_pdu.data, my_name);
            sendto(sd, &send_pdu, sizeof(send_pdu), 0, (struct sockaddr *)&server, len);
            printf("Goodbye!\n");
            break;
        }
    }

    // close UDP socket, end program
    close(sd);
    return 0;
}



