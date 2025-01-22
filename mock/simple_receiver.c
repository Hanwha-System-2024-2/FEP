#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>


typedef struct {
	int tr_id;
	int length;
} hdr;

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


typedef struct {
    int id;
    char type;
    char message[50];
} MyStruct;

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
    server_addr.sin_port = htons(SERVER_PORT);

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
    while(1){
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
    }
    

    // Close sockets
    close(client_fd);
    close(server_fd);
    return 0;
}
