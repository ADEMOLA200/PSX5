#include <stdio.h>
#include <string.h>
int main(){
    FILE *f = fopen("out_from_test.txt","wb");
    if(!f){ printf("open failed\n"); return 1; }
    const char *s = "sample data from ELF\n";
    fwrite(s, 1, strlen(s), f);
    fclose(f);
    printf("wrote file\n");
    return 0;
}
