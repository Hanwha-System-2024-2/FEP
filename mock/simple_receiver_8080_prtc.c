#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <mqueue.h>
#include <time.h>
#include <errno.h>

// Define ports and timeout
#define SERVER_PORT 8080
#define TIMEOUT_MS 20000  // 20 seconds timeout
#define EXECUTION_MQ "/execution_queue"

// Define `hdr` structure
typedef struct {
    int tr_id;
    int length;
} hdr;

// Define `fkq_order` structure (incoming orders)
typedef struct {
    hdr hdr;
    char stock_code[7];
    char padding1;
    char stock_name[51];
    char padding2;
    char transaction_code[7];
    char padding3;
    char user_id[21];
    char padding4[3];
    char order_type;
    char padding5[3];
    int quantity;
    char order_time[15];
    char padding6;
    int price;
    char original_order[7];
    char padding7;
} fkq_order;

// Define `kft_execution` structure (execution response to MQ)
typedef struct {
    hdr hdr;
    char transaction_code[7];
    char padding1;
    int status_code;
    char time[15];
    char padding2;
    int executed_price;
    char original_order[7];
    char padding3;
    char reject_code[7];
    char padding4;
} kft_execution;

// Define `fot_order_is_submitted` structure (initial handshake response)
typedef struct {
    hdr hdr;
    char transaction_code[7];
    char padding1;
    char user_id[21];
    char padding2[3];
    char time[15];
    char padding3;
    char reject_code[7];
    char padding4;
} fot_order_is_submitted;

// Global variables
int client_fd = -1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Get current time in YYYYMMDDHHMMSS format
void get_current_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y%m%d%H%M%S", tm_info);
}

// Function to handle handshake
int handshake(int client_fd) {
    fkq_order order;
    memset(&order, 0, sizeof(order));

    ssize_t bytes_received = recv(client_fd, &order, sizeof(order), 0);
    if (bytes_received <= 0) {
        printf("Handshake failed. Client disconnected.\n");
        close(client_fd);
        return 0;
    }

    printf("Handshake Order Received:\n");
    printf("  tr_id: %d\n", order.hdr.tr_id);
    printf("  stock_code: %s\n", order.stock_code);
    printf("  transaction_code: %s\n", order.transaction_code);
    printf("  user_id: %s\n", order.user_id);
    printf("  order_time: %s\n", order.order_time);
    printf("  price: %d\n", order.price);
    printf("  quantity: %d\n", order.quantity);

    // Prepare handshake response
    fot_order_is_submitted response;
    memset(&response, 0, sizeof(response));

    response.hdr.tr_id = 82;  // Response ID
    response.hdr.length = sizeof(fot_order_is_submitted);
    strncpy(response.transaction_code, "Y", sizeof(response.transaction_code) - 1);
    strncpy(response.user_id, order.user_id, sizeof(response.user_id) - 1);

    // Get current time
    get_current_time(response.time, sizeof(response.time));
    strncpy(response.reject_code, "0000", sizeof(response.reject_code) - 1); // OK response

    // Send response
    if (send(client_fd, &response, sizeof(response), 0) < 0) {
        perror("Failed to send handshake response");
        close(client_fd);
        return 0;
    }

    printf("Handshake successful. Response sent to client.\n");
    return 1;
}


// Function to receive execution messages and enqueue them into message queue
void *execution_receiver(void *arg) {
    mqd_t mq;
    struct mq_attr attr;
    kft_execution exec_msg;

    // Set message queue attributes
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(kft_execution);
    attr.mq_curmsgs = 0;

    // Open or create message queue
    mq = mq_open(EXECUTION_MQ, O_WRONLY | O_CREAT, 0644, &attr);
    if (mq == -1) {
        perror("Message queue open failed");
        return NULL;
    }

    while (1) {
        if (client_fd > 0) {
            fkq_order received_order;

            struct timeval timeout;
            timeout.tv_sec = 20;  // 20 seconds timeout
            timeout.tv_usec = 0;  // 0 microseconds
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

            ssize_t bytes_received = recv(client_fd, &received_order, sizeof(received_order), 0);
            
            if (bytes_received > 0) {
                printf("Received order from client. Preparing execution message...\n");
                if(received_order.hdr.tr_id==83) continue;
                // Prepare execution response for MQ
                kft_execution exec;
                memset(&exec, 0, sizeof(exec));
                exec.hdr.tr_id = 11;
                exec.hdr.length = sizeof(kft_execution);
                strncpy(exec.transaction_code, received_order.transaction_code, sizeof(exec.transaction_code) - 1);
                strncpy(exec.time, "20250123134500", sizeof(exec.time) - 1);
                exec.status_code = 0;  // Assume success
                exec.executed_price = received_order.price;
                strncpy(exec.original_order, received_order.original_order, sizeof(exec.original_order) - 1);
                strncpy(exec.reject_code, "0000", sizeof(exec.reject_code) - 1); // No error

                // Prepare `fot_order_is_submitted` structure
                fot_order_is_submitted submit_result;            
                memset(&submit_result, 0, sizeof(fot_order_is_submitted)); // Initialize the struct
                submit_result.hdr.tr_id = 10;
                submit_result.hdr.length = sizeof(fot_order_is_submitted);
            
                strncpy(submit_result.transaction_code, received_order.transaction_code, sizeof(submit_result.transaction_code) - 1);
                submit_result.transaction_code[sizeof(submit_result.transaction_code) - 1] = '\0'; // Null-terminate
                
                strncpy(submit_result.user_id, received_order.user_id, sizeof(submit_result.user_id));
                submit_result.user_id[sizeof(submit_result.user_id) - 1] = '\0'; // Null-terminate
            
                strncpy(submit_result.time, "20250123134500", sizeof(submit_result.time) - 1);
                submit_result.time[sizeof(submit_result.time) - 1] = '\0'; // Null-terminate

                strncpy(submit_result.reject_code, "0000", sizeof(submit_result.reject_code) - 1); // No error
                submit_result.reject_code[sizeof(submit_result.reject_code) - 1] = '\0'; // Null-terminate

                // send order_submitted response

                ssize_t bytes_sent = send(client_fd, &submit_result, sizeof(fot_order_is_submitted), 0);
                    if (bytes_sent < 0) {
                        perror("Failed to send response");
                    } else if (bytes_sent < sizeof(submit_result)) {
                        fprintf(stderr, "Partial response sent. Expected %lu bytes, sent %ld bytes.\n",
                                sizeof(submit_result), bytes_sent);
                    } else {
                        printf("Response sent successfully to client. Sent %ld bytes.\n", bytes_sent);
                    }

                // Enqueue execution message to MQ
                if (mq_send(mq, (const char *)&exec, sizeof(kft_execution), 0) == -1) {
                    perror("mq_send failed");
                } else {
                    printf("Execution message successfully enqueued in MQ.\n");
                }
            } else if (bytes_received == 0) {
                printf("Client disconnected.\n");
                close(client_fd);
                client_fd = -1;
            } else {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    printf("No message received for 20 seconds. Disconnecting client.\n");
                    close(client_fd);
                    client_fd = -1;                
                }
                perror("Receive failed");
            }
        }
        sleep(1);
    }

    mq_close(mq);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pthread_t receiver_thread;

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Allow reuse of address/port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("setsockopt(SO_REUSEADDR) failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


    // Bind socket to port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    pthread_create(&receiver_thread, NULL, execution_receiver, NULL);
 
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd > 0 && handshake(client_fd)) {
            printf("Client connected successfully.\n");
        }
    }

    return 0;
}
