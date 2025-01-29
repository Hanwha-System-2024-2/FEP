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
} KRX_W_count;

#define QUEUE_NAME "/krx_wc_queue"
// socket

#define MAX_CLIENTS 20

void print_kft_execution(const kft_execution *execution) {
    printf("kft_execution:\n");
    printf("  Header:\n");
    printf("    tr_id: %d\n", execution->hdr.tr_id);
    printf("    length: %d\n", execution->hdr.length);
    printf("  transaction_code: %s\n", execution->transaction_code);
    printf("  status_code: %d\n", execution->status_code);
    printf("  time: %s\n", execution->time);
    printf("  executed_price: %d\n", execution->executed_price);
    printf("  original_order: %s\n", execution->original_order);
    printf("  reject_code: %s\n", execution->reject_code);
}

void save_order_to_file_bin(kft_execution *execution, char filepath[256]) {
        FILE *file = fopen(filepath, "ab"); // 바이너리 쓰기 모드
        fwrite(execution, sizeof(kft_execution), 1, file);    
        fclose(file);
}


int main() {

    mqd_t mq;
    struct mq_attr attr = {0};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 100;   // Maximum number of messages in the queue
    attr.mq_msgsize = sizeof(kft_execution); // Maximum size of each message in bytes
    attr.mq_curmsgs = 0;   // Current number of messages in the queue
    
    // mmap memory code
    const char *shared_mem_name = "/KRX_W_count";
    const size_t shared_mem_size = sizeof(KRX_W_count);
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
    KRX_W_count *w_count = mmap(NULL, shared_mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (w_count == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        shm_unlink(shared_mem_name);
        exit(EXIT_FAILURE);
    }

    w_count->wc = 0;

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

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    address.sin_port = htons(FEP_KRX_R_PORT);

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
    printf("Server listening on port %d\n", FEP_KRX_R_PORT);

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
        snprintf(filepath, sizeof(filepath), "%s/krx_received_data.txt", home_dir);
    } else {
        // Fallback to current directory if $HOME is not set
        strncpy(filepath, "./krx_received_data.txt", sizeof(filepath));
    }

    // Open the message queue
    mq = mq_open(QUEUE_NAME, O_CREAT | O_WRONLY, 0644, NULL, &attr);
    if (mq == -1) {
        perror("mq_open");
        exit(1);
    }
    
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
                kft_execution execution;
                ssize_t bytes_received = recv(fds[i].fd, &execution, sizeof(execution), 0);
                if (bytes_received <= 0) {
                    // Connection closed or error
                    printf("Client disconnected\n");
                    close(fds[i].fd);
                    fds[i].fd = -1;
                // } else if (bytes_received == sizeof(received_order.hdr.length)) {
                } else if (bytes_received == sizeof(kft_execution)) {
                    if (execution.hdr.tr_id !=11 ) { // Example valid range
                    printf("skip to process Invalid tr_id: %d\n", execution.hdr.tr_id);
                    continue; // Skip processing
                    }
                    printf("Execution received successfully, DB status will be updated.\n");
                    print_kft_execution(&execution);
                    // db update

                }      
            }
        }
    }

    close(server_fd);
    // Close the message queue
    mq_close(mq);

    return 0;
}