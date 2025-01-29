#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <envs.h>


int main() {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    const char *server = "3.35.134.101";  // MySQL 서버 주소
    const char *user = "root";         // 사용자 이름
    const char *password = "1234"; // 사용자 비밀번호
    const char *database = "tx_history";  // 데이터베이스 이름

    // MySQL 초기화
    conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        return EXIT_FAILURE;
    }

    // 데이터베이스 연결
    if (mysql_real_connect(conn, server, user, password, database, 0, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return EXIT_FAILURE;
    }

    // INSERT 쿼리
    const char *insert_query1 = "INSERT INTO tx_history (stock_code, stock_name, transaction_code, user_id, order_type, quantity, order_time, price, original_order, status) VALUES ('AAPL01', 'Apple Inc.', 'TX1234', 'user123', 'B', 100, '2025-01-24 10:15:00', 150, NULL, 'P')";
    const char *insert_query2 = "INSERT INTO tx_history (stock_code, stock_name, transaction_code, user_id, order_type, quantity, order_time, price, original_order, status) VALUES ('GOOG01', 'Google LLC', 'TX1235', 'user456', 'S', 50, '2025-01-24 11:30:00', 2800, 'TX1233', 'C')";

    if (mysql_query(conn, insert_query1)) {
        fprintf(stderr, "INSERT query 1 failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return EXIT_FAILURE;
    }
    if (mysql_query(conn, insert_query2)) {
        fprintf(stderr, "INSERT query 2 failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return EXIT_FAILURE;
    }
    printf("Data inserted successfully!\n");

    // SELECT 쿼리
    const char *select_query = "SELECT transaction_code, stock_name, user_id, quantity, price, order_time FROM tx_history";
    if (mysql_query(conn, select_query)) {
        fprintf(stderr, "SELECT query failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return EXIT_FAILURE;
    }

    // 결과 가져오기
    res = mysql_store_result(conn);
    if (res == NULL) {
        fprintf(stderr, "mysql_store_result() failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return EXIT_FAILURE;
    }

    // 결과 출력
    printf("\nTransaction Code | Stock Name       | User ID  | Quantity | Price | Order Time\n");
    printf("------------------------------------------------------------------------------\n");
    while ((row = mysql_fetch_row(res)) != NULL) {
        printf("%-16s | %-15s | %-8s | %-8s | %-5s | %s\n", row[0], row[1], row[2], row[3], row[4], row[5]);
    }

    // 메모리 해제
    mysql_free_result(res);

    // 연결 닫기
    mysql_close(conn);

    return EXIT_SUCCESS;
}
