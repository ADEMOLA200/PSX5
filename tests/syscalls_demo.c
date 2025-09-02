#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
int main(){
    int fd = open("sys_demo.txt", O_CREAT | O_WRONLY, 0644);
    if(fd >= 0){
        const char *s = "syscall demo\\n";
        write(fd, s, 13);
        close(fd);
    }
    printf("pid=%d time=%ld\\n", getpid(), time(NULL));
    return 0;
}
