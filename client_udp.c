#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEGMENT_NUMBER 10 // Defines how many segment to send at one go
#define BUFFER_SIZE 500   // Defines buffer sizes while sending and receiving payload

// Struct that carries payload
struct packet {
    int sequence;
    size_t size_remaining;
    char data[BUFFER_SIZE];
};

// Struct that carries acknowledgements
struct ack {
    int sequence;
    char data[SEGMENT_NUMBER];
};

// Initializes packet[] and acknowledgement structs with default values
void initialize_packets(struct packet packets[], struct ack *ack_ptr) {
    ack_ptr->sequence = -1;
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        packets[counter].sequence = 0;
        packets[counter].size_remaining = 0;
        ack_ptr->data[counter] = '-';
    }
}

// Fills packets by reading file
// Returns: SUCCESS -> Number of packets filled successfully
//          EOF     ->  0
//          ERROR   -> -1
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
        if (read_result != 1 && packets[counter].size_remaining == 0) {
            if (feof(file)) {
                return number_of_packets;
            }
            printf("\n[ERR] Error reading file!\n");
            return -1;
        }
    }
    return number_of_packets;
}

// Waits till all acknowledgements received & resend lost packets
// Returns:  0 -> All acknowledgements received
//          -1 -> Error while sending lost packets
int wait_for_all_ack(int socket_handler, struct ack *ack_ptr, struct packet packets[], struct sockaddr_in server) {

    // Wait till ack received
    recvfrom(socket_handler, ack_ptr, sizeof(struct ack), MSG_WAITFORONE,
             NULL, NULL);

    // Flag initialized for further waiting
    int should_wait_further = 0;

    // Check acknowledgements and resend lost packets
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        if (ack_ptr->data[counter] == '0') {
            int resend_handler = sendto(socket_handler, &packets[counter], sizeof(struct packet), 0,
                                        (struct sockaddr *) &server, sizeof(server));
            if (resend_handler == -1) {
                printf("\n[ERR] Error resending packet!\n");
                return -1;
            }

            // Flag set for waiting further
            should_wait_further = 1;
        }
    }

    return should_wait_further == 0 ? should_wait_further : wait_for_all_ack(socket_handler, ack_ptr, packets, server);
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
        printf("\n[ERR] Error creating the socket!\n");
        return -1;
    }
    printf("[RES] Socket created successfully!\r");

    // Initialize server address
    struct sockaddr_in server;
    inet_pton(AF_INET, "25.134.219.159", &(server.sin_addr));
    server.sin_family = AF_INET;
    server.sin_port = htons(server_port);

    // Initialize file, sequence & size variables
    int sequence = 0;
    FILE *file;
    file = fopen("../video.mov", "r");
    fseek(file, 0, SEEK_END);
    size_t file_size_remaining = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Initialize packet and ack structs
    struct packet packets[SEGMENT_NUMBER];
    struct ack ack_ptr;
    initialize_packets(packets, &ack_ptr);

    // Initialize program state variables
    size_t total_size = file_size_remaining;
    time_t time_start = time(NULL);

    // Loop till file sent
    for (;;) {
        // Fill packets by reading file
        int number_of_packets = fill_all_packets(file, packets, &file_size_remaining, &sequence);

        // Send all filled packets
        for (int counter = 0; counter < number_of_packets; counter++) {
            int send_handler = sendto(socket_handler, &packets[counter], sizeof(packets[counter]), 0,
                                      (struct sockaddr *) &server, sizeof(server));
            if (send_handler == -1) {
                printf("\n[ERR] Error sending packet!\n");
                return -1;
            }


            printf("[ACT] Uploaded: %lu%% | Time elapsed: %lds\r",
                   ((total_size - file_size_remaining) * 100) / total_size,
                   time(NULL) - time_start);
            fflush(stdout);
        }

        // Wait for acknowledgements
        int ack_handler = wait_for_all_ack(socket_handler, &ack_ptr, packets, server);
        if (ack_handler == -1) {
            return -1;
        }

        // If all file sent
        if (file_size_remaining == 0) {
            printf("\n[RES] Successfully sent the file!\n");
            break;
        }
    }

    // Clean up file descriptors
    fclose(file);
    close(socket_handler);

    return 0;
}
