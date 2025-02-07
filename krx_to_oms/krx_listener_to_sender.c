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
#include <unistd.h>  // For sleep()

#define HEARTBEAT_INTERVAL 15  // Send heartbeat every 15 seconds
#define HEARTBEAT_TR_ID 83     // Unique tr_id for heartbeat messages
#define LOG_FILE_PATH "/home/ubuntu/logs/krx_listener_prtc.log"
#define ORDER_TIME_FORMAT "%Y%m%d%H%M%S"


int sock;

typedef struct {
    int wc; // Write counter
} KRX_W_count;


#define QUEUE_NAME "/execution_wc_queue"
FILE *log_file = NULL;

// Heartbeat function (runs in a separate thread)
void *heartbeat_thread(void *arg) {
    while (1) {
        sleep(HEARTBEAT_INTERVAL);  // Wait for 15 seconds

        fkq_order heartbeat_msg;
        memset(&heartbeat_msg, 0, sizeof(fkq_order));

        heartbeat_msg.hdr.tr_id = HEARTBEAT_TR_ID;
        heartbeat_msg.hdr.length = sizeof(fkq_order);
        
        ssize_t sent_bytes = send(sock, &heartbeat_msg, sizeof(heartbeat_msg), 0);
        if (sent_bytes < 0) {
            log_message("ERROR", "heartbeat", "Failed to send heartbeat: %s\n", strerror(errno));
            close(sock);
            exit(EXIT_FAILURE);
        }

        log_message("INFO", "heartbeat", "Heartbeat sent to KRX\n");
    }

    return NULL;
}

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

void save_order_to_file_bin(kft_execution *execution, FILE *file) {
        fwrite(execution, sizeof(kft_execution), 1, file);    
        fflush(file);
}

int is_order_time_future(const char *order_time) {

    struct tm order_tm = {0};
    time_t order_epoch, current_time;

    // Convert order_time string to struct tm
    if (strptime(order_time, ORDER_TIME_FORMAT, &order_tm) == NULL) {
        fprintf(stderr, "Error parsing order_time: %s\n", order_time);
        return -1; // Error case
    }

    // Convert struct tm to epoch time
    order_epoch = mktime(&order_tm);
    if (order_epoch == -1) {
        fprintf(stderr, "Error converting order_time to epoch\n");
        return -1;
    }

    // Get the current epoch time
    current_time = time(NULL);
    if (current_time == -1) {
        fprintf(stderr, "Error getting current time\n");
        return -1;
    }

    // Check if current time is more than 2 second past order time
    if (order_epoch > current_time + 2) {
        return 1; // order time is future => error
    } else {
        return 0; // within range
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

int initial_handshake(int sock) {
    fkq_order init_handshake_data;
    memset(&init_handshake_data, 0, sizeof(init_handshake_data));
    
    init_handshake_data.hdr.tr_id = 81; // Set tr_id to 81
    init_handshake_data.hdr.length = sizeof(fot_order_is_submitted);
    
    strncpy(init_handshake_data.transaction_code, "21", sizeof(init_handshake_data.transaction_code) - 1);
    init_handshake_data.transaction_code[sizeof(init_handshake_data.transaction_code) - 1] = '\0'; // Null-terminate

    // 서버로 초기 데이터 전송
    ssize_t sent_byte = send(sock, &init_handshake_data, sizeof(init_handshake_data), 0);
    if (sent_byte < 0) {
        log_message("ERROR", "socket", "Failed to send initial handshake data\n");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    log_message("INFO", "socket", "Initial handshake data sent\n");
    
    // 서버로부터 응답 대기
    fot_order_is_submitted response;
    memset(&response, 0, sizeof(response));
    
    ssize_t bytes_received = recv(sock, &response, sizeof(response), 0);
    if (bytes_received <= 0) {
        log_message("ERROR", "socket", "Failed to receive initial handshake response\n");
        close(sock);
        exit(EXIT_FAILURE);
    }
    

    if(response.hdr.tr_id == 82){
        if(response.transaction_code[0] == 'Y'){
            return 1;
        } else if (response.transaction_code[0] == 'N'){
            return 0;
        }
        // 에러, 이상한 값이라고
        log_message("ERROR", "handshake", "Error: invalid transaction_code(Y or N).\n");
        return 0; 
    } else {
        // 에러, 잘못된 handshake tr id라고.
        log_message("ERROR", "handshake", "Error: invalid response tr_id.\n"); 
        return 0;
    }
}


int main() {

    init_log(); 

    // set timezone as KST
    setenv("TZ", "Asia/Seoul", 1);
    tzset(); // Apply the changes
    mqd_t mq;
    struct mq_attr attr = {0};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 200;   // Maximum number of messages in the queue
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

    // Initialize shared memory if it is newly created
    if (is_initialized) {
        w_count->wc = 0; // Initialize write counter to 0
        log_message("DEBUG", "shm", "Shared memory initialized. wr = %d\n", w_count->wc);
        log_message("DEBUG", "shm","Shared memory initialized. PID: %d\n", getpid());
    } else {
        log_message("DEBUG", "shm", "Shared memory already exists. wc = %d\n", w_count->wc);
    }

    // socket code
    struct sockaddr_in server_addr;

    // 소켓 생성
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        log_message("ERROR", "socket", "Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 서버 주소 설정
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(KRX_EXEC_PORT);
    if (inet_pton(AF_INET, KRX_IP, &server_addr.sin_addr) <= 0) {
        log_message("ERROR", "socket","Invalid IP address or format");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // 서버 연결
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_message("ERROR", "socket","Connection to the server failed");
        close(sock);
        exit(EXIT_FAILURE);
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

    FILE *file = fopen(filepath, "ab");
    if (file == NULL) {
        log_message("ERROR", "file", "Error opening file");
        return;
    }

    // Open the message queue
    mq = mq_open(QUEUE_NAME, O_CREAT | O_WRONLY, 0644, NULL, &attr);
    if (mq == -1) {
        perror("mq_open");
        exit(1);
    }

    while (1) {
        if (initial_handshake(sock) == 1) {
            // Handshake successful, proceed with the program
            break;
        }

        log_message("WARN", "handshake", "Initial handshake failed. Retrying in 1 second...\n");
        sleep(1);  // Wait 1 second before retrying
    }
    log_message("INFO", "socket", "Initial handshake was successful and connected.\n");

    // **Start heartbeat thread**
    pthread_t hb_thread;
    if (pthread_create(&hb_thread, NULL, heartbeat_thread, NULL) != 0) {
        log_message("ERROR", "thread", "Failed to create heartbeat thread\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    while (1) {
        kft_execution execution;
        ssize_t bytes_received = recv(sock, &execution, sizeof(execution), 0);
        if (bytes_received <= 0) {
            // Connection closed or error
            log_message("ERROR", "socket","Client disconnected\n");
            close(sock);
            sock = -1;
        // } else if (bytes_received == sizeof(received_order.hdr.length)) {
        } else if (bytes_received == sizeof(kft_execution)) {
            // validation
            if (execution.hdr.tr_id !=11 ) { 
                log_message("INFO", "validation", "skip to process Invalid tr_id: %d\n", execution.hdr.tr_id);
                continue; 
            } else if (!((execution.status_code == 0) || (execution.status_code == 1) || (execution.status_code == 99))) { 
                log_message("INFO", "validation", "invalid krx execution status code : %s.\n", "E301");
                continue; 
            } else if (is_order_time_future(execution.time)) { 
                log_message("INFO", "validation", "invalid krx execution time : %s.\n", "E302");
                continue; 
            } else if (execution.executed_price < 0){
                log_message("INFO", "validation", "invalid krx execution executed price : %s.\n", "E303");
                continue; // Skip processing
            } else if (!((strncmp(execution.reject_code, "0000", 4) == 0) || (execution.reject_code[0] == 'E'))){
                log_message("INFO", "validation", "invalid krx execution reject code : %s.\n", "E304");
                continue; // Skip processing
            }

            log_message("INFO", "execution", "execution result arrived\n");
            print_kft_execution(&execution);

            save_order_to_file_bin(&execution, file);
            log_message("INFO", "FILE", "execution is written to a file");
            w_count->wc++;
            log_message("INFO", "shm", "krx_wc increased. wc = %d\n", w_count->wc);

            //send wc
            if(mq_send(mq, (char *)&w_count->wc, sizeof(int), 0)==-1){
                log_message("ERROR", "mq", "mq_send failed: could be mq full");
                exit(1);
            }           
            log_message("DEBUG", "mq", "krx_w_cnt sent: %d", w_count->wc);
                
        } else {
            log_message("ERROR", "socket", "Incomplete data received. Expected %lu bytes, got %ld bytes.\n", sizeof(kft_execution), bytes_received);
        }   
    }
    close(sock);
    // Close the message queue
    mq_close(mq);
    
    return 0;
}

