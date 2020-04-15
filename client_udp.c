#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define SEGMENT_NUMBER 10
#define BUFFER_SIZE 500

struct packet {
    int sequence;
    size_t size_remaining;
    char data[BUFFER_SIZE];
};

struct ack {
    int sequence;
    char data[SEGMENT_NUMBER];
};

void initialize_packets(struct packet packets[]) {
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        packets[counter].sequence = 0;
        packets[counter].size_remaining = 0;
    }
}

void initialize_ack(struct ack *ack_ptr) {
    ack_ptr->sequence = -1;
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        ack_ptr->data[counter] = '-';
    }
}

int wait_for_all_ack(int socket_handler, struct ack *ack_ptr, struct packet packets[], struct sockaddr_in server) {
    size_t len = sizeof(server);
    initialize_ack(ack_ptr);
    recvfrom(socket_handler, ack_ptr, sizeof(struct ack), MSG_WAITFORONE,
             (struct sockaddr *) &server, (socklen_t *) &len);

    // Check acknowledgements and resend lost packets
    int should_wait_further = 0;
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        if (ack_ptr->data[counter] == '0') {
            int resend_handler = sendto(socket_handler, &packets[counter], sizeof(struct packet), 0,
                                        (struct sockaddr *) &server, sizeof(server));
            should_wait_further = 1;
            if (resend_handler == -1) {
                printf("Err: Error resending packet!\n");
                return -1;
            }
        }
    }

    if (should_wait_further == 0) {
        return 0;
    }
    return wait_for_all_ack(socket_handler, ack_ptr, packets, server);
}

// Returns:
//  SUCCESS -> Number of packets filled successfully
//  EOF ->  0
//  ERROR -> -1
int fill_all_packets(FILE *file, struct packet packets[], size_t *file_size_remaining,
                     int *sequence) {
    int number_of_packets = 0;
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        // Fill packet
        size_t read_result = fread(packets[counter].data, BUFFER_SIZE, 1, file);
        packets[counter].size_remaining = *file_size_remaining;
        packets[counter].sequence = *sequence;

        // Update loop variables
        number_of_packets++;
        *sequence += 1;
        *file_size_remaining -=
                packets[counter].size_remaining >= BUFFER_SIZE ? BUFFER_SIZE : packets[counter].size_remaining;

        // Check guard conditions
        if (read_result != 1) {
            if (feof(file)) {
                *file_size_remaining = 0;
                return number_of_packets;
            }
            printf("Err: Error reading file!");
            return -1;
        }
    }
    return number_of_packets;
}

int main(int argc, char *argv[]) {

    // Ensure correct arguments
    if (argc < 2) {
        printf("usage: %s port", argv[0]);
        return -1;
    }

    // Parse argument to server port
    long server_port = strtol(argv[1], NULL, 10);

    // Initialize socket
    int socket_handler = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_handler == -1) {
        printf("Err: Could not connect to the socket!\n");
        return -1;
    }
    printf("Res: Socket created successfully!\n");

    // Initialize server address
    struct sockaddr_in server;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;
    server.sin_port = htons(server_port);

    // Initialize file, sequence & size variables
    int sequence = 0;
    FILE *file;
    file = fopen("../video.mov", "r");
    fseek(file, 0, SEEK_END);
    size_t file_size_remaining = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Initialize packet
    struct packet packets[SEGMENT_NUMBER];
    initialize_packets(packets);

    // Initialize ack & number of packets to be sent
    struct ack ack_ptr;
    int number_of_packets = 0;

    for (;;) {
        number_of_packets = fill_all_packets(file, packets, &file_size_remaining, &sequence);
        if (number_of_packets <= 0) {
            break;
        }

        for (int counter = 0; counter < number_of_packets; counter++) {
            int send_handler = sendto(socket_handler, &packets[counter], sizeof(packets[counter]), 0,
                                      (struct sockaddr *) &server, sizeof(server));
            printf("Sending %lu\n", packets[counter].size_remaining);
            if (send_handler == -1) {
                printf("Err: Error sending packet!\n");
                return -1;
            }
        }

        int ack_handler = wait_for_all_ack(socket_handler, &ack_ptr, packets, server);
        if (ack_handler == -1) {
            return -1;
        }
    }

    fclose(file);

    close(socket_handler);
    return 0;
}
