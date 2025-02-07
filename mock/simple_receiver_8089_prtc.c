#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <mqueue.h>

// Define port and timeout
#define SERVER_PORT 8089
#define TIMEOUT_MS 20000  // 20 seconds timeout

// Define message queue name
#define EXECUTION_MQ "/execution_queue"

// Header struct
typedef struct {
    int tr_id;
    int length;
} hdr;

// Order struct (incoming)
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
} ofq_order;

// Execution response struct (outgoing)
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

// Execution struct (message queue)
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
} execution_message;


// Global client socket
int client_fd = -1;

// Function to get current time in YYYYMMDDHHMMSS format
void get_current_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y%m%d%H%M%S", tm_info);
}

// Function to handle handshake
int handshake(int client_fd) {
    ofq_order order;
    memset(&order, 0, sizeof(order));

    ssize_t bytes_received = recv(client_fd, &order, sizeof(order), 0);
    if (bytes_received <= 0) {
        printf("Handshake failed. Client disconnected.\n");
        close(client_fd);
        return 0;
    }

    // Print received order details
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

// Function to handle execution messages from the message queue
void *execution_listener(void *arg) {
    mqd_t mq;
    struct mq_attr attr;
    execution_message exec_msg;

    // Open message queue
    mq = mq_open(EXECUTION_MQ, O_RDONLY | O_CREAT, 0644, NULL);
    if (mq == -1) {
        perror("Message queue open failed");
        return NULL;
    }

    // Get queue attributes
    if (mq_getattr(mq, &attr) == -1) {
        perror("mq_getattr failed");
        mq_close(mq);
        return NULL;
    }

    while (1) {
        // Wait for execution message
        ssize_t bytes_read = mq_receive(mq, (char *)&exec_msg, sizeof(execution_message), NULL);
        if (bytes_read > 0) {
            printf("Execution message received from MQ. Forwarding to client...\n");

            if (client_fd > 0) {
                if (send(client_fd, &exec_msg, sizeof(execution_message), 0) < 0) {
                    perror("Failed to send execution message");
                } else {
                    printf("Execution message sent to client.\n");
                }
            }
        }
    }

    mq_close(mq);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pthread_t mq_thread;

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind socket to port 8089
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

    // Start execution message listener thread
    if (pthread_create(&mq_thread, NULL, execution_listener, NULL) != 0) {
        perror("Failed to create MQ listener thread");
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Accept new connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        printf("Client connected.\n");

        // Perform handshake
        if (!handshake(client_fd)) {
            client_fd = -1;
            continue;
        }

        // Poll setup for heartbeat timeout
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;

        while (1) {
            int poll_result = poll(&pfd, 1, TIMEOUT_MS);  // Wait for data or timeout

            if (poll_result == 0) {
                printf("No message received for 20 seconds. Disconnecting client.\n");
                close(client_fd);
                client_fd = -1;
                break;
            } else if (poll_result < 0) {
                perror("Poll error");
                close(client_fd);
                client_fd = -1;
                break;
            }

            // Read heartbeat or other messages
            char buffer[1024];
            ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_received <= 0) {
                printf("Client disconnected.\n");
                close(client_fd);
                client_fd = -1;
                break;
            }

            printf("Heartbeat received from client.\n");
        }
    }

    // Close server socket
    close(server_fd);
    pthread_join(mq_thread, NULL);
    return 0;
}
