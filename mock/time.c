
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ORDER_TIME_FORMAT "%Y%m%d%H%M%S"

int main() {

    struct tm order_tm = {0};
       struct tm order_tm2 = {0};
    time_t order_epoch, order_epoch2, current_time;
    char* order_time = "20250101120000";
    char* order_time2 = "20250101120005";
    // Convert order_time string to struct tm
    if (strptime(order_time, ORDER_TIME_FORMAT, &order_tm) == NULL) {
        fprintf(stderr, "Error parsing order_time: %s\n", order_time);
        return -1; // Error case
    }
       if (strptime(order_time2, ORDER_TIME_FORMAT, &order_tm2) == NULL) {
        fprintf(stderr, "Error parsing order_time: %s\n", order_time2);
        return -1; // Error case
    }

    // Convert struct tm to epoch time
    order_epoch = mktime(&order_tm);
    if (order_epoch == -1) {
        fprintf(stderr, "Error converting order_time to epoch\n");
        return -1;
    }
        // Convert struct tm to epoch time
    order_epoch2 = mktime(&order_tm2);
    if (order_epoch2 == -1) {
        fprintf(stderr, "Error converting order_time to epoch\n");
        return -1;
    }

    // Get the current epoch time
    current_time = time(NULL);
    if (current_time == -1) {
        fprintf(stderr, "Error getting current time\n");
        return -1;
    }

    printf("Current local time: %s", asctime(localtime(&current_time)));
    printf("Order time local time: %s", asctime(localtime(&order_epoch)));
        printf("Order time2 local time: %s", asctime(localtime(&order_epoch2)));

    // printf("Current local time : %s", asctime(current_time));
    // printf("ordertime local time : %s", asctime(order_epoch));
    
    // Check if current time is more than 2 second past order time
    double diff1 = difftime(order_epoch2, order_epoch); // 미래, 과거, 결과는 초
    printf("difference = %f\n", diff1);

        double diff2 = difftime(current_time, order_epoch); // 미래, 과거, 결과는 초
    printf("difference2 = %f\n", diff2);

    if (diff2 < 2) {
        printf("current time is past than order time"); // future
    } else {
        printf("order time time more future than cur"); // future
    }

    // // Set the TZ environment variable to Korea Standard Time
    setenv("TZ", "Asia/Seoul", 1);
    tzset(); // Apply the changes

    // // Now get the current time
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);

    // // Print the local time in Korea Standard Time
    printf("Current local time (KST): %s", asctime(local_time));

    return 0;
}
