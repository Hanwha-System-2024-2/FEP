#ifndef OMSFEPKRX_STRUCT_H
#define OMSFEPKRX_STRUCT_H

// 헤더 구성
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
    hdr hdr;
    char transaction_code[7]; // 거래코드 
    char user_id[21];         // 유저 ID 
    char time[15];            // 응답시간 (YYYYMMDDHHMMSS)
    char reject_code[7];      // 거부사유코드 (문자열)
} kft_order;

typedef struct {
    hdr hdr;
    char transaction_code[7]; // 거래코드 
    int status_code;          // 상태 코드 (0: 체결, 1: 취소, 99: 오류)
    char time[15];            // 응답시간 (YYYYMMDDHHMMSS)
    int executed_price;       // 체결 가격
    char original_order[7];   // 원주문번호
    char reject_code[7];      // 거부사유코드 (문자열)
} kft_execution;

typedef struct{
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
} ofq_order;


#endif //OMSFEPKRX_STRUCT_H