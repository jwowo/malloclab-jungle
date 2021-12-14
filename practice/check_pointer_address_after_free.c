#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int *ptr = malloc(sizeof(int));

    printf("inital value using malloc : %d\n", *ptr);
    printf("initial address using malloc : %p\n", ptr);
    
    // assign value to integer pointer
    *ptr = 10;

    // check value & address 
    printf("value after assignment : %d\n", *ptr);
    printf("address after assignment :%p\n", ptr);

    // check value & address after free allocated memory 
    free(ptr);
    printf("value after free allocated memory : %d\n", *ptr);
    printf("address after free allocated memory : %p\n", ptr);

    

    /*
    Findings : 
        - free(ptr) 로 할당된 메모리를 반환하여도 해당 포인터(ptr)는   
          여전히 malloc된 블록의 시작 주소를 가리킨다.
    */
}