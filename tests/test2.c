#include <stdio.h>
int main(){ FILE* f = fopen("out.txt","wb"); if(f){ const char *s="test data\n"; fwrite(s,1,strlen(s),f); fclose(f); } return 0; }
