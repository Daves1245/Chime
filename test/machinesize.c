#include <stdio.h>

int main() {
	printf("int: %d\n", sizeof int);
	printf("32/64: %d %d\n", sizeof uint32_t, uint64_t);
	return 0;
}
