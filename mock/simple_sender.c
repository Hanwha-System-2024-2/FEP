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
    int sock;
    struct sockaddr_in server_addr;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Prepare data
    hdr header;
    header.length = sizeof(fkq_order);
    // strncpy(header.tr_id,"0000",sizeof(header.tr_id));
    // header.tr_id[sizeof(header.tr_id) - 1] = '\0'; // Null-terminate
    header.tr_id = 9;

    fkq_order order;
    order.hdr = header;
    strncpy(order.stock_code,"stock1",sizeof(order.stock_code));
    // order.stock_code = "stock1";
    order.stock_code[sizeof(order.stock_code) - 1] = '\0'; // Null-terminate

    strncpy(order.stock_name,"Hanwha Systems",sizeof(order.stock_name));
    order.stock_name[sizeof(order.stock_name) - 1] = '\0'; // Null-terminate

    strncpy(order.transaction_code,"tx1",sizeof(order.transaction_code));
    order.transaction_code[sizeof(order.transaction_code) - 1] = '\0'; // Null-terminate

    strncpy(order.user_id,"testusr1",sizeof(order.user_id));
    order.user_id[sizeof(order.user_id) - 1] = '\0'; // Null-terminate

    order.order_type='S';
    order.quantity = 17;
    
    strncpy(order.order_time,"20250121094930",sizeof(order.order_time));
    order.order_time[sizeof(order.order_time) - 1] = '\0'; // Null-terminate

    order.price=43000;
    
    strncpy(order.original_order,"tx99",sizeof(order.original_order));
    order.original_order[sizeof(order.original_order) - 1] = '\0'; // Null-terminate

    // MyStruct data;
    // data.id = 123;
    // data.type = 'A';
    // strncpy(data.message, "Hello, TCP!", sizeof(data.message));

    // Send struct
    if (send(sock, &order, sizeof(order), 0) < 0) {
        perror("Send failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // printf("Data sent: id=%d, type=%c, message=%s\n", data.id, data.type, data.message);

    // Close socket
    close(sock);
    return 0;
}
