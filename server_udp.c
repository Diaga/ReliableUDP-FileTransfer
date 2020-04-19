#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include <unistd.h>
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

// Binds the socket to specified port
// Returns: -1 -> Error while binding
int bind_socket(int socket_handler, long port) {
    int bind_handler;

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(port);

    bind_handler = bind(socket_handler, (struct sockaddr *) &local, sizeof(local));
    return bind_handler;
}

// Initializes packet[] and acknowledgement structs with default values
void initialize_packets(struct packet packets[], struct ack *ack_ptr) {
    ack_ptr->sequence = -1;
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        packets[counter].sequence = -1;
        packets[counter].size_remaining = -1;
        ack_ptr->data[counter] = '-';
    }
}

// Copy received packet to packet[] struct at the required order
void copy_packet(struct packet pckt, struct packet packets[], int global_sequence) {
    if (pckt.sequence <= global_sequence) {
        return;
    }
    int index = pckt.sequence % SEGMENT_NUMBER;
    packets[index].sequence = pckt.sequence;
    packets[index].size_remaining = pckt.size_remaining;
    memcpy(packets[index].data, pckt.data, sizeof(pckt.data));
}

// Checks if all packets have been received, updates ack packet accordingly
// Returns: Number of packets received -> All packets received
//          -1                         -> Not received
int check_all_packets_received(struct packet packets[], struct ack *ack_ptr) {
    int received_all = 1;
    for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
        if (packets[counter].sequence == -1) {
            ack_ptr->data[counter] = '0';
            received_all = -1;
        }
        ack_ptr->data[counter] = '1';
        if (packets[counter].size_remaining == 0) {
            return counter;
        }
    }
    return received_all == 1 ? SEGMENT_NUMBER - 1 : received_all;
}

// Write data in packet[] struct to file
// Returns:  0 -> Successfully written data
//           1 -> Last packet received
//          -1 -> Error while reading
int write_data_to_file(FILE *file, struct packet packets[], int index) {
    for (int counter = 0; counter <= index; counter++) {
        size_t size = packets[counter].size_remaining > BUFFER_SIZE ? BUFFER_SIZE : packets[counter].size_remaining;
        size_t write_handler = fwrite(packets[counter].data, size, 1, file);

        if (packets[counter].size_remaining == 0) {
            return 1;
        }
        if (write_handler != 1) {
            return -1;
        }
    }
    return 0;
}

// Entry point of the program
int main(int argc, char *argv[]) {

    // Ensure correct arguments
    if (argc < 2) {
        printf("usage: %s port", argv[0]);
        return -1;
    }

    // Parse argument to server port
    long port = strtol(argv[1], NULL, 10);

    // Client address struct
    struct sockaddr_in client;
    size_t len = sizeof(client);

    // Initialize socket
    int socket_handler = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_handler == -1) {
        printf("\n[ERR] Error creating the socket!\n");
        return -1;
    }
    printf("[RES] Socket created successfully!\r");

    // Bind socket to port
    if (bind_socket(socket_handler, port) < 0) {
        printf("\n[ERR] Error binding the port!\n");
        return -1;
    }
    printf("[RES] Port %lu bound successfully!\r", port);
    printf("[ACT] Waiting for client to connect...\r");
    fflush(stdout);

    // Initialize file pointer
    FILE *file;
    file = fopen("video_received.mov", "w");
    if (file == NULL) {
        printf("\n[ERR] Error opening file for writing!\n");
    }

    // Initialize packets & ack
    struct packet packets[SEGMENT_NUMBER];
    struct ack ack_ptr;
    ack_ptr.sequence = 0;
    struct packet pckt;
    pckt.sequence = -1;
    initialize_packets(packets, &ack_ptr);

    // Initialize program state
    int sleep_counter = 0, client_connected = 0, global_sequence = -1;
    time_t time_start;
    size_t total_size;

    // Loop till client connects & sends file
    for (;;) {
        // If client has connected
        if (sleep_counter == 0 && client_connected == 1) {
            // Check if all SEGMENT_NUMBER packets have been received
            int check_handler = check_all_packets_received(packets, &ack_ptr);
            if (check_handler != -1) {
                // Write packets to file
                write_data_to_file(file, packets, check_handler);

                // Send ack to client about all of SEGMENT_NUMBER received packets
                int ack_handler = sendto(socket_handler, &ack_ptr, sizeof(ack_ptr), 0,
                                         (const struct sockaddr *) &client, sizeof(client));
                if (ack_handler == -1) {
                    printf("\n[ERR] Error sending acknowledgements!\n");
                    return -1;
                }

                // If last packet was received
                if (packets[check_handler].size_remaining == 0) {
                    // Clean up file descriptors
                    fclose(file);
                    close(socket_handler);

                    printf("\n[RES] Successfully received the file!\n");
                    return 0;
                }

                // Reinitialize packets and ack pointer to receive more packets
                initialize_packets(packets, &ack_ptr);
            }
        } else if (sleep_counter == 2) {
            // Mark received and unreceived packets
            for (int counter = 0; counter < SEGMENT_NUMBER; counter++) {
                if (packets[counter].sequence == -1) {
                    ack_ptr.data[counter] = '0';
                } else {
                    ack_ptr.data[counter] = '1';
                }
            }

            // Send acknowledgments
            int ack_handler = sendto(socket_handler, &ack_ptr, sizeof(ack_ptr), 0,
                                     (const struct sockaddr *) &client, sizeof(client));
            if (ack_handler == -1) {
                printf("\n[ERR] Error sending acknowledgements!\n");
                return -1;
            }

            // Reset sleep counter
            sleep_counter = 0;
        }

        // Receive packets
        recvfrom(socket_handler, &pckt, sizeof(pckt), MSG_DONTWAIT,
                 (struct sockaddr *) &client, (socklen_t *) &len);

        if (pckt.sequence != -1) {
            // Set program in run state
            if (client_connected == 0) {
                client_connected = 1;
                time_start = time(NULL);
                total_size = pckt.size_remaining + BUFFER_SIZE;
            }

            // Reset sleep counter
            sleep_counter = 0;

            // Copy if packet is not a duplicate
            copy_packet(pckt, packets, global_sequence);

            printf("[ACT] Downloaded: %lu%% | Time elapsed: %lds\r",
                   ((total_size - pckt.size_remaining) * 100) / total_size,
                   time(NULL) - time_start);
            fflush(stdout);

            // Reset pckt state
            pckt.sequence = -1;
        } else {
            // No packet received for 0.1s
            if (client_connected == 1) {
                sleep_counter++;
            }

            // Sleep for 0.1s
            struct timespec time_sleep, time_remaining;
            time_sleep.tv_sec = 0;
            time_sleep.tv_nsec = 100000000L;
            nanosleep(&time_sleep, &time_remaining);
        }
    }
}