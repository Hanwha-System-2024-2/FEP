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

// shared memory
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    int rc;
} R_count;


#define QUEUE_NAME "/wc_queue"

// socket
#define MAX_CLIENTS 20


int main() {

    mqd_t mq;
    struct mq_attr attr;

    void remove_spaces(char *str) {
        int i = 0, j = 0;

        // Loop through the string
        while (str[i] != '\0') {
            // Copy only non-space characters
            if (str[i] != ' ') {
                str[j] = str[i];
                j++;
            }
            i++;
        }

        // Null-terminate the modified string
        str[j] = '\0';
    }

    // TCP 송신 함수
    void send_order_to_krx(fkq_order *order) {
        int sock;
        struct sockaddr_in server_addr;

        // 소켓 생성
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        // 서버 주소 설정
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(KRX_PORT);
        if (inet_pton(AF_INET, KRX_IP, &server_addr.sin_addr) <= 0) {
            perror("Invalid IP address or format");
            close(sock);
            exit(EXIT_FAILURE);
        }

        // 서버 연결
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection to the server failed");
            close(sock);
            exit(EXIT_FAILURE);
        }

        // 구조체 데이터 전송
        ssize_t sent_byte = send(sock, order, sizeof(order), 0);
        if (sent_byte < 0) {
            perror("Failed to send data");
            close(sock);
            exit(EXIT_FAILURE);
        } else {
            printf("Order sent successfully to %s:%d %d byte\n", KRX_IP, KRX_PORT, sent_byte);
        }

        // 소켓 종료
        // close(sock);
    }

    void file_int_assign(int *address, FILE *file){
        char buffer[12];
        fread(buffer, sizeof(char), 11, file);
        buffer[11] = '\0';
        *address = atoi(buffer);
    }

    void read_order_from_file_by_fixed_len(const char *filepath, int start, int end, R_count *r_count) {
        fkq_order order;
        memset(&order, 0, sizeof(order)); // Initialize the struct

        FILE *file = fopen(filepath, "rb");
        if (file == NULL) {
            perror("Error opening file");
            return;
        }

        // Calculate the position of the line
        size_t struct_size = sizeof(order);

        while(end > r_count->rc){
            // int를 11byte로 대응시켜서 하드코딩.. refactor 필요
            if (fseek(file, 153 * r_count->rc, SEEK_SET) != 0) {
            perror("Failed to seek to line");
            fclose(file);
            exit(EXIT_FAILURE);
            }
            // Read the struct from the file
            file_int_assign(&order.hdr.tr_id, file);
            file_int_assign(&order.hdr.length, file);
            fread(order.stock_code, sizeof(order.stock_code), 1, file);
            remove_spaces(order.stock_code);
            fread(order.stock_name, sizeof(order.stock_name), 1, file);
            remove_spaces(order.stock_name);
            fread(order.transaction_code, sizeof(order.transaction_code), 1, file);
            remove_spaces(order.transaction_code);
            fread(order.user_id, sizeof(order.user_id), 1, file);
            remove_spaces(order.user_id);

            fread(&order.order_type, sizeof(order.order_type), 1, file);
            file_int_assign(&order.quantity, file);
            fread(order.order_time, sizeof(order.order_time), 1, file);
            file_int_assign(&order.price, file);
            fread(order.original_order, sizeof(order.original_order), 1, file);
            remove_spaces(order.original_order);

            // add null
            order.stock_name[sizeof(order.stock_name) - 1] = '\0'; // Null-terminate
            order.transaction_code[sizeof(order.transaction_code) - 1] = '\0'; // Null-terminate
            order.user_id[sizeof(order.user_id) - 1] = '\0'; // Null-terminate
            order.order_time[sizeof(order.order_time) - 1] = '\0'; // Null-terminate
            order.original_order[sizeof(order.original_order) - 1] = '\0'; // Null-terminate

            send_order_to_krx(&order);
            r_count->rc++;
            printf("current value rc = %d\n", r_count->rc);
            
            printf("%d,%d,%s,%s,%s,%s,%c,%d,%s,%d,%s\n",
                            order.hdr.tr_id,
                            order.hdr.length,
                            order.stock_code,
                            order.stock_name,
                            order.transaction_code,
                            order.user_id,
                            order.order_type,
                            order.quantity,
                            order.order_time,
                            order.price,
                            order.original_order);

        }

        fclose(file);
    }

    // mmap memory code
    const char *shared_mem_name = "/R_count";
    const size_t shared_mem_size = sizeof(R_count);
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
    R_count *r_count = mmap(NULL, shared_mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (r_count == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        shm_unlink(shared_mem_name);
        exit(EXIT_FAILURE);
    }

    r_count->rc = 0; // Initialize write counter to 0


    // Initialize shared memory if it is newly created
    if (is_initialized) {
        r_count->rc = 0; // Initialize write counter to 0
        printf("Shared memory initialized. rc = %d\n", r_count->rc);
        printf("Shared memory initialized. PID: %d\n", getpid());
    } else {
        printf("Shared memory already exists. rc = %d\n", r_count->rc);
    }

    // socket code
    int server_fd, new_socket, activity;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // set file dir structure
    const char *home_dir = getenv("HOME");
    char filepath[256];
    if (home_dir != NULL) {
        snprintf(filepath, sizeof(filepath), "%s/received_data.txt", home_dir);
    } else {
        // Fallback to current directory if $HOME is not set
        strncpy(filepath, "./received_data.txt", sizeof(filepath));
    }
    printf("test1\n");
    // Open the message queue
    mq = mq_open(QUEUE_NAME, O_RDONLY);
    if (mq == -1) {
        perror("mq_open");
        exit(1);
    }
    printf("message queue opened\n");
    
    // Get queue attributes
    if (mq_getattr(mq, &attr) == -1) {
        perror("mq_getattr");
        exit(1);
    }

    int received_wc; 

    while(1){
        // Receive the message
        ssize_t bytes_read = mq_receive(mq, (char *)&received_wc, attr.mq_msgsize, NULL);
        if (bytes_read == -1) {
            perror("mq_receive");
            mq_close(mq);
            exit(1);
        }
        // Convert the byte array back to a long
        printf("Received: %d\n", received_wc);
        if(received_wc > r_count->rc){
            // read_lines_from_file(filepath, r_count->rc, received_wc, r_count);
            read_order_from_file_by_fixed_len(filepath, r_count->rc, received_wc, r_count);
        }

    }
    
    // // Close and unlink the message queue
    // mq_close(mq);
    // mq_unlink(QUEUE_NAME);

    return 0;
}
