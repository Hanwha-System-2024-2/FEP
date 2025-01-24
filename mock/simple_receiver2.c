#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <envs.h>

// Define `hdr` structure
typedef struct {
    int tr_id;
    int length;
} hdr;

// Define `fkq_order` structure
typedef struct {
    hdr hdr;
    char stock_code[7];       // 종목코드
    char stock_name[51];      // 종목명 
    char transaction_code[7]; // 거래코드 
    char user_id[21];         // 유저 ID 
    char order_type;          // 매수(B) / 매도(S) (1바이트)
    int quantity;             // 수량 (정수형)
    char order_time[15];      // 주문시간 (YYYYMMDDHHMMSS)
    int price;                // 호가 (정수형)
    char original_order[7];   // 원주문번호 (문자열)
} fkq_order;

// Define `kft_execution` structure
typedef struct {
    hdr hdr;
    char transaction_code[7]; // 거래코드
    int status_code;          // 상태 코드 (0: 체결, 1: 취소, 99: 오류)
    char time[15];            // 응답시간 (YYYYMMDDHHMMSS)
    int executed_price;       // 체결 가격
    char original_order[7];   // 원주문번호
    char reject_code[7];      // 거부사유코드 (문자열)
} kft_execution;

// Function to send `kft_execution` structure to the specified IP and port
void send_kft_execution(const kft_execution *exec) {
    int sock;
    struct sockaddr_in server_addr;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(FEP_KRX_R_PORT);
    if (inet_pton(AF_INET, FEP_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address or format");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Send the `kft_execution` structure
    ssize_t sent_bytes = send(sock, exec, sizeof(kft_execution), 0);
    if (sent_bytes < 0) {
        perror("Send failed");
    } else if (sent_bytes < sizeof(kft_execution)) {
        fprintf(stderr, "Partial data sent. Expected %lu bytes, sent %ld bytes.\n",
                sizeof(kft_execution), sent_bytes);
    } else {
        printf("krx says transaction is executed\n");
    }

    // Close the socket
    close(sock);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(FEP_OMS_R_PORT);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        printf("Waiting for connections...\n");

        // Accept a connection
        if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len)) < 0) {
            perror("Accept failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        printf("Client connected!\n");

        // Receive data
        fkq_order received_order;
        if (recv(client_fd, &received_order, sizeof(received_order), 0) < 0) {
            perror("Receive failed");
            close(client_fd);
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        printf("krx get order\n");
        // Print received data
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

        // Prepare `kft_execution` structure
        kft_execution exec_response = {
            .hdr = {2, sizeof(kft_execution)},
            .status_code = 0, // Success
        };
        strncpy(exec_response.transaction_code, received_order.transaction_code, sizeof(exec_response.transaction_code) - 1);
        strncpy(exec_response.time, "20250123134500", sizeof(exec_response.time) - 1);
        exec_response.executed_price = received_order.price; // Example
        strncpy(exec_response.original_order, received_order.original_order, sizeof(exec_response.original_order) - 1);
        strncpy(exec_response.reject_code, "0000", sizeof(exec_response.reject_code) - 1); // No error

        // Send `kft_execution` to the remote server
        send_kft_execution(&exec_response);
    }

    // Close sockets
    close(client_fd);
    close(server_fd);
    return 0;
}
