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
#define BUFFER_SIZE 1024

void print_kft_order(const kft_order *order) {
    printf("kft_order:\n");
    printf("  Header:\n");
    printf("    tr_id: %d\n", order->hdr.tr_id);
    printf("    length: %d\n", order->hdr.length);
    printf("  transaction_code: %s\n", order->transaction_code);
    printf("  user_id: %s\n", order->user_id);
    printf("  time: %s\n", order->time);
    printf("  reject_code: %s\n", order->reject_code);
}

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

// void pad_and_save(const char *src, size_t size, FILE *file) {
//     char tmp[size];
//     memset(tmp, ' ', size);       // Fill the entire field with spaces
//     size_t copy_len = strlen(src); // Determine the actual content length
//     if (copy_len > size) {
//         copy_len = size;           // Truncate if content exceeds field size
//     }
//     memcpy(tmp, src, copy_len);
//     fwrite(tmp , size, 1, file);        

// }

// void save_int_to_file(int value, FILE *file){
//         char buffer[11];
//         memset(buffer, '0', sizeof(buffer)); // 배열을 '0'으로 초기화
    
//         // 숫자를 문자열로 변환하고 buffer 끝부분에 채움
//         snprintf(buffer + (11 - snprintf(NULL, 0, "%d", value)), sizeof(buffer), "%d", value);

//         // printf("converted: %d\n", network_order);
//         // 파일에 저장 (고정 크기: 4바이트)
//         // fwrite(&network_order, sizeof(network_order), 1, file);
//         size_t written = fwrite(buffer, sizeof(buffer), 1, file);
//         if (written != 1) {
//             perror("Failed to write to file");
//         }

//     }

// void save_order_to_file(fkq_order *order, char filepath[256]) {
//         FILE *file = fopen(filepath, "ab"); // 바이너리 쓰기 모드
//         if (file == NULL) {
//             perror("파일 열기 실패");
//             exit(EXIT_FAILURE);
//         }

//         // Fill fields with fixed-length padding

//         save_int_to_file(order->hdr.tr_id, file);
//         save_int_to_file(order->hdr.length, file);

//         pad_and_save(order->stock_code, sizeof(order->stock_code), file);
//         pad_and_save(order->stock_name, sizeof(order->stock_name), file);
//         pad_and_save(order->transaction_code, sizeof(order->transaction_code), file);
//         pad_and_save(order->user_id, sizeof(order->user_id), file);

//         fputc(order->order_type, file);
//         save_int_to_file(order->quantity, file);
//         pad_and_save(order->order_time, sizeof(order->order_time), file);
//         save_int_to_file(order->price, file);
//         pad_and_save(order->original_order, sizeof(order->original_order), file);

//         fclose(file);
//     }


int main() {

    mqd_t mq;
    struct mq_attr attr = {0};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;   // Maximum number of messages in the queue
    attr.mq_msgsize = sizeof(int); // Maximum size of each message in bytes
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
    
    //56
    // kft_execution temp1;
    // printf("kft_execution size  = %d\n", sizeof(temp1));
    
    //60
    // kft_order temp2;
    // printf("kft_order size = %d\n", sizeof(temp2));
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

                ssize_t bytes_received;
                char buffer[80]; // Large enough buffer for both structs
                
                // Receive data from the socket
                bytes_received = recv(fds[i].fd, buffer, sizeof(buffer), 0);

                if (bytes_received <= 0) {
                    perror("recv failed or connection closed");
                    return 1;
                }

                printf("Received %ld bytes\n", bytes_received);

                if (bytes_received == sizeof(kft_execution)) {
                    kft_execution execution_result;
                    memcpy(&execution_result, buffer, sizeof(kft_execution));
                    print_kft_execution(&execution_result);

                } else if (bytes_received == sizeof(kft_order)) {
                    kft_order order_accepted;
                    memcpy(&order_accepted, buffer, sizeof(kft_order));
                    print_kft_order(&order_accepted);

                }
            }
        }
    }

    close(server_fd);
    // Close the message queue
    mq_close(mq);

    return 0;
}