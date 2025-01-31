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

//log
#include <stdarg.h>
#include <time.h>

typedef struct {
    int wc; // Write counter
} KRX_W_count;

#define QUEUE_NAME "/execution_wc_queue"

// socket
#define MAX_CLIENTS 20
#define LOG_FILE_PATH "/home/ubuntu/logs/krx_listener.log"
FILE *log_file = NULL;


// Initialize logging
void init_log() {
    mkdir("/home/ubuntu/logs", 0777);
    log_file = fopen(LOG_FILE_PATH, "a");
    if (!log_file) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
}

// Log function with level and module
void log_message(const char *level, const char *module, const char *format, ...) {
    if (!log_file) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buffer[20];

    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log_file, "[%s] [%s] [%s] ", time_buffer, level, module);

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fflush(log_file);  //  Ensure data is written immediately
}

// Function to clean up the log file
void close_log() {
    if (log_file) {
        fflush(log_file);  // Ensure all data is written before closing
        fclose(log_file);
    }
}

void print_kft_execution(const kft_execution *execution) {
    log_message("INFO", "execution", "krx_execution:\n");
    log_message("INFO", "execution","  Header:\n");
    log_message("INFO", "execution", "    tr_id: %d\n", execution->hdr.tr_id);
    log_message("INFO", "execution","    length: %d\n", execution->hdr.length);
    log_message("INFO", "execution","  transaction_code: %s\n", execution->transaction_code);
    log_message("INFO", "execution","  status_code: %d\n", execution->status_code);
    log_message("INFO", "execution","  time: %s\n", execution->time);
    log_message("INFO", "execution","  executed_price: %d\n", execution->executed_price);
    log_message("INFO", "execution","  original_order: %s\n", execution->original_order);
    log_message("INFO", "execution","  reject_code: %s\n", execution->reject_code);
}

void save_order_to_file_bin(kft_execution *execution, FILE *file) {
        fwrite(execution, sizeof(kft_execution), 1, file);    
        fflush(file);
}


int main() {
    printf("0");
    fflush(stdout);
    init_log();
    mqd_t mq;
    struct mq_attr attr = {0};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 200;   // Maximum number of messages in the queue
    attr.mq_msgsize = sizeof(int); // Maximum size of each message in bytes
    attr.mq_curmsgs = 0;   // Current number of messages in the queue
    printf("1");
    
    // mmap memory code
    const char *shared_mem_name = "/KRX_W_count";
    const size_t shared_mem_size = sizeof(KRX_W_count);
    int is_initialized = 0; // Flag to track if shared memory is newly created
    printf("2");
    fflush(stdout);
   // Create or open the shared memory object
    int shm_fd = shm_open(shared_mem_name, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd == -1) {
        if (errno == EEXIST) {
            // Shared memory already exists
            shm_fd = shm_open(shared_mem_name, O_RDWR, 0666);
            if (shm_fd == -1) {
                log_message("ERROR", "shm", "shm_open failed");
                exit(EXIT_FAILURE);
            }
        } else {
            log_message("ERROR", "shm", "shm_open failed");
            exit(EXIT_FAILURE);
        }
    } else {
        // This is the first time the shared memory is being created
        is_initialized = 1;
    }
printf("3");
fflush(stdout);
    // Set size of the shared memory object
    if (ftruncate(shm_fd, shared_mem_size) == -1) {
        log_message("ERROR", "shm", "ftruncate failed");
        close(shm_fd);
        shm_unlink(shared_mem_name);
        exit(EXIT_FAILURE);
    }

    // Map the shared memory object
    KRX_W_count *w_count = mmap(NULL, shared_mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (w_count == MAP_FAILED) {
        log_message("ERROR", "shm","mmap failed");
        close(shm_fd);
        shm_unlink(shared_mem_name);
        exit(EXIT_FAILURE);
    }
printf("4");
fflush(stdout);
    // Initialize shared memory if it is newly created
    if (is_initialized) {
        w_count->wc = 0; // Initialize write counter to 0
        log_message("DEBUG", "shm", "Shared memory initialized. wr = %d\n", w_count->wc);
        log_message("DEBUG", "shm","Shared memory initialized. PID: %d\n", getpid());
    } else {
        log_message("DEBUG", "shm", "Shared memory already exists. wc = %d\n", w_count->wc);
    }
printf("5");
fflush(stdout);
    // socket code
    int server_fd, activity;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        log_message("ERROR", "socket", "Socket failed");
        exit(EXIT_FAILURE);
    }
printf("6");
fflush(stdout);
    // Configure server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    address.sin_port = htons(FEP_KRX_R_PORT);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log_message("ERROR", "socket","Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        log_message("ERROR", "socket", "Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("7");
    fflush(stdout);
    int client_sockets[MAX_CLIENTS] = {0}; // Track client sockets
    log_message("DEBUG", "socket", "Server listening on port %d\n", FEP_KRX_R_PORT);

    // Poll array to monitor multiple file descriptors
    struct pollfd fds[MAX_CLIENTS];

    // Initialize poll array
    fds[0].fd = server_fd;  // Monitor the server socket for new connections
    fds[0].events = POLLIN; // Monitor for incoming data

    for (int i = 1; i < MAX_CLIENTS; i++) {
        fds[i].fd = -1; // Initialize all other file descriptors
    }
printf("8");
fflush(stdout);
    // set file dir structure
    const char *home_dir = getenv("HOME");
    char filepath[256];
    if (home_dir != NULL) {
        snprintf(filepath, sizeof(filepath), "%s/krx_received_data.txt", home_dir);
    } else {
        // Fallback to current directory if $HOME is not set
        strncpy(filepath, "./krx_received_data.txt", sizeof(filepath));
    }

    FILE *file = fopen(filepath, "ab");
    if (file == NULL) {
        log_message("ERROR", "file", "Error opening file");
        return;
    }
printf("9");
fflush(stdout);
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
                    if (strcmp(execution.reject_code, "0000") == 0) {
                        printf("Reject code is 0000: Order is successful\n");
                        printf("Execution received successfully, DB status will be updated.\n");
                        print_kft_execution(&execution);

                        save_order_to_file_bin(&execution, file);
                        w_count->wc++;
                        log_message("INFO", "shm", "krx_wc increased. wc = %d\n", w_count->wc);
                        printf("krx_wc increased. wc = %d\n", w_count->wc);

                        //send wc
                        if(mq_send(mq, (char *)&w_count->wc, sizeof(int), 0)==-1){
                            log_message("ERROR", "mq", "mq_send failed: could be mq full");
                            exit(1);
                        }           
                        log_message("DEBUG", "mq", "krx_w_cnt sent: %d", w_count->wc);
                        
                    } else {
                        printf("Reject code is NOT 0000: Order failed, reason: %s\n", execution.reject_code);
                    }
                }      
            }
        }
    }

    close(server_fd);
    // Close the message queue
    mq_close(mq);

    return 0;
}