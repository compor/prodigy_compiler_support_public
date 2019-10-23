#include <stdlib.h> 

void initialize(int * a, int size)
{
	for (int i = 0; i < size; ++i) {
		*(a + i) = i;
	}
}

int main(int argc, char * argv[])
{
	int * a = (int*)calloc(10, sizeof(int));
	int * b = (int*)malloc(10 * sizeof(int));

	initialize(a,10);
	initialize(b,10);

	int c = b[a[0]];
}
