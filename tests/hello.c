#include <stdio.h>
#include <time.h>
int main(){
    printf("Hello from test ELF\n");
    time_t t = time(NULL);
    printf("time: %ld\n", (long)t);
    return 0;
}
