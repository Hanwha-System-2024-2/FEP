#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h> // For open()
#include <errno.h>
#include <mqueue.h>
#include <oms_fep_krx_struct.h>
#include <envs.h>

// shared memory
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    int wc; // Write counter
} W_count;

#define QUEUE_NAME "/wc_queue"
#define SUBMIT_QUEUE_NAME "/submit_queue"
// socket
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

// Function to print the elements of fot_order_is_submitted
void print_fot_order_is_submitted(const fot_order_is_submitted *submit_result) {
    printf("Header:\n");
    printf("  Transaction ID: %d\n", submit_result->hdr.tr_id);
    printf("  Length: %d\n", submit_result->hdr.length);
    printf("Transaction Code: %s\n", submit_result->transaction_code);
    printf("User ID: %s\n", submit_result->user_id);
    printf("Response Time: %s\n", submit_result->time);
    printf("Reject Code: %s\n", submit_result->reject_code);
}

void save_int_to_file(int value, FILE *file){
        char buffer[11];
        memset(buffer, '0', sizeof(buffer)); // 배열을 '0'으로 초기화
    
        // 숫자를 문자열로 변환하고 buffer 끝부분에 채움
        snprintf(buffer + (11 - snprintf(NULL, 0, "%d", value)), sizeof(buffer), "%d", value);

        // printf("converted: %d\n", network_order);
        // 파일에 저장 (고정 크기: 4바이트)
        // fwrite(&network_order, sizeof(network_order), 1, file);
        size_t written = fwrite(buffer, sizeof(buffer), 1, file);
        if (written != 1) {
            perror("Failed to write to file");
        }

    }

void save_order_to_file_bin(fkq_order *order, char filepath[256]) {
        FILE *file = fopen(filepath, "ab"); // 바이너리 쓰기 모드
        fwrite(order, sizeof(fkq_order), 1, file);    
        fclose(file);

}

void print_buffer_contents(const char *buffer, size_t size) {
    printf("Buffer contents (size = %lu):\n", size);
    for (size_t i = 0; i < size; i++) {
        // Print each byte in hexadecimal and as a character
        printf("0x%02X (%c) ", (unsigned char)buffer[i],
               (buffer[i] >= 32 && buffer[i] <= 126) ? buffer[i] : '.');
        if ((i + 1) % 16 == 0) {
            printf("\n"); // Newline every 16 bytes for readability
        }
    }
    printf("\n");
}


int main() {

    mqd_t mq, submit_mq;
    struct mq_attr attr = {0};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 100;   // Maximum number of messages in the queue
    attr.mq_msgsize = sizeof(int); // Maximum size of each message in bytes
    attr.mq_curmsgs = 0;   // Current number of messages in the queue
    
    struct mq_attr submit_attr = {0};

    // mmap memory code
    const char *shared_mem_name = "/W_count";
    const size_t shared_mem_size = sizeof(W_count);
    int is_initialized = 0; // Flag to track if shared memory is newly created

   // Create or open the shared memory object
    int shm_fd = shm_open(shared_mem_name, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd == -1) {
        if (errno == EEXIST) {
            // Shared memory already exists
            shm_fd = shm_open(shared_mem_name, O_RDWR, 0666);
            if (shm_fd == -1) {
                perror("shm_open failed");
                exit(EXIT_FAILURE);
            }
        } else {
            perror("shm_open failed");
            exit(EXIT_FAILURE);
        }
    } else {
        // This is the first time the shared memory is being created
        is_initialized = 1;
    }

    // Set size of the shared memory object
    if (ftruncate(shm_fd, shared_mem_size) == -1) {
        perror("ftruncate failed");
        close(shm_fd);
        shm_unlink(shared_mem_name);
        exit(EXIT_FAILURE);
    }


    // Map the shared memory object
    W_count *w_count = mmap(NULL, shared_mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (w_count == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        shm_unlink(shared_mem_name);
        exit(EXIT_FAILURE);
    }

    // Initialize shared memory if it is newly created
    if (is_initialized) {
        w_count->wc = 0; // Initialize write counter to 0
        printf("Shared memory initialized. wr = %d\n", w_count->wc);
        printf("Shared memory initialized. PID: %d\n", getpid());
    } else {
        printf("Shared memory already exists. wc = %d\n", w_count->wc);
    }

    // socket code
    int server_fd, activity;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Buffer for receiving data
    char buffer[BUFFER_SIZE];

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    address.sin_port = htons(FEP_OMS_R_PORT);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    int client_sockets[MAX_CLIENTS] = {0}; // Track client sockets
    printf("Server listening on port %d\n", FEP_OMS_R_PORT);

    // Poll array to monitor multiple file descriptors
    struct pollfd fds[MAX_CLIENTS];

    // Initialize poll array
    fds[0].fd = server_fd;  // Monitor the server socket for new connections
    fds[0].events = POLLIN; // Monitor for incoming data

    for (int i = 1; i < MAX_CLIENTS; i++) {
        fds[i].fd = -1; // Initialize all other file descriptors
    }

    // set file dir structure
    const char *home_dir = getenv("HOME");
    char filepath[256];
    if (home_dir != NULL) {
        snprintf(filepath, sizeof(filepath), "%s/received_data.txt", home_dir);
    } else {
        // Fallback to current directory if $HOME is not set
        strncpy(filepath, "./received_data.txt", sizeof(filepath));
    }

    // Open the message queue
    mq = mq_open(QUEUE_NAME, O_CREAT | O_WRONLY, 0644, NULL, &attr);
    if (mq == -1) {
        perror("mq_open");
        exit(1);
    }
    printf("wc queue opened.\n");

     // Open the sender queue
    submit_mq = mq_open(SUBMIT_QUEUE_NAME, O_RDONLY);
    if (submit_mq == -1) {
        perror("mq_open (sender)");
        mq_close(mq);
        exit(EXIT_FAILURE);
    }

      // Get queue attributes
    if (mq_getattr(submit_mq, &submit_attr) == -1) {
        perror("mq_getattr");
        mq_close(mq);
        exit(EXIT_FAILURE);
    }
    printf("submit message queue opened.\n");

    
    while (1) {
        // Wait for an event
        activity = poll(fds, MAX_CLIENTS, -1); // Infinite timeout

        if (activity < 0) {
            perror("Poll error");
            break;
        }

        // Check if the server socket is ready (new incoming connection)
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

            if (client_fd < 0) {
                perror("Accept failed");
                continue;
            }

            printf("New connection from %s:%d\n",
                   inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // Add new socket to poll array
            for (int i = 1; i < MAX_CLIENTS; i++) {
                if (fds[i].fd == -1) {
                    fds[i].fd = client_fd;
                    fds[i].events = POLLIN; // Monitor for incoming data
                    // client_sockets[i] = new_socket;
                    break;
                }
            }
        }

        // Check all client sockets for activity
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (fds[i].fd != -1 && (fds[i].revents & POLLIN)) {
                fkq_order received_order;
                ssize_t bytes_received = recv(fds[i].fd, &received_order, sizeof(received_order), 0);
                if (bytes_received <= 0) {
                    // Connection closed or error
                    printf("Client disconnected\n");
                    close(fds[i].fd);
                    fds[i].fd = -1;
                // } else if (bytes_received == sizeof(received_order.hdr.length)) {
                } else if (bytes_received == sizeof(received_order)) {

     
                    if (received_order.hdr.tr_id !=9 ) { // Example valid range
                    printf("skip to process Invalid tr_id: %d\n", received_order.hdr.tr_id);
                    continue; // Skip processing
                    }
                   printf("Order received successfully.\n");
                    printf("%d,%d,%s,%s,%s,%s,%c,%d,%s,%d,%s\n",
                            received_order.hdr.tr_id,
                            received_order.hdr.length,
                            received_order.stock_code,
                            received_order.stock_name,
                            received_order.transaction_code,
                            received_order.user_id,
                            received_order.order_type,
                            received_order.quantity,
                            received_order.order_time,
                            received_order.price,
                            received_order.original_order);
                    
                    // Save the order to file
                    save_order_to_file_bin(&received_order, filepath);
                    w_count->wc++;
                    printf("wc increased. wc = %d\n", w_count->wc);

                    //send wc
                    if(mq_send(mq, (char *)&w_count->wc, sizeof(int), 0)==-1){
                        perror("mq_send");
                        exit(1);
                    }           
                    printf("Msg sent: %d", w_count->wc);   

                    // receive submit_result from queue
                    fot_order_is_submitted submit_result;

                    ssize_t bytes_read = mq_receive(submit_mq, (char *)&submit_result, submit_attr.mq_msgsize, NULL);
                    // ssize_t bytes_read = mq_receive(submit_mq, buffer, sizeof(fot_order_is_submitted), NULL);

                    if (bytes_read == -1) {
                        perror("submit_mq_receive");
                        mq_close(submit_mq);
                        exit(1);
                    }

                    print_fot_order_is_submitted(&submit_result);


                    // send back to oms by connected socket
                        // for jmeter load test
                    // int response_size = sizeof(submit_result);
                    // send(fds[i].fd, &response_size, sizeof(response_size), 0); // Send size first
                    char ack_message[] = "ACK\n";
                    send(fds[i].fd, ack_message, sizeof(ack_message), 0);

                    ssize_t bytes_sent = send(fds[i].fd, &submit_result, sizeof(fot_order_is_submitted), 0);
                    if (bytes_sent < 0) {
                        perror("Failed to send data to connected socket");
                    } else if (bytes_sent < sizeof(fot_order_is_submitted)) {
                        fprintf(stderr, "Partial data sent. Expected %lu bytes, sent %ld bytes.\n",
                                sizeof(fot_order_is_submitted), bytes_sent);
                    } else {
                        printf("Successfully sent response to OMS via connected socket. Sent %ld bytes.\n", bytes_sent);
                    }

                } else {
                    printf(stderr, "Incomplete data received. Expected %lu bytes, got %ld bytes.\n", sizeof(fkq_order), bytes_received);
                }    
            }
        }
    }

    close(server_fd);
    // Close the message queue
    mq_close(mq);

    return 0;
}