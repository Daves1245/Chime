#include <string.h>
#include <stdio.h>

int main() {
    char file_name[100] = "myfile.txt";
    char tmp[100] = {0};
    strcpy(tmp, file_name);
    sprintf(file_name, "saves/%s", tmp);
    printf("%s\n", file_name);
    return 0;
}
