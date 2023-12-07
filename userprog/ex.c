#include <stdio.h>
#include <string.h>

int main(void){
    char *token;
	char *saveptr;
    char file_name[256];
    const char file_ex[] = "echo    x   y    z";
    strcpy(file_name, file_ex);
    // First call
	token = strtok_r(file_name, " ", &saveptr);
	while(token!=NULL){
		printf("token : %s\n", token);
		if(*saveptr=='\0')	saveptr++;
		token = strtok_r(NULL, " ", &saveptr);
	}
    return 0;
}