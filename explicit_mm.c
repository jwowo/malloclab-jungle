#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "SWJungle_week06_10",
    /* First member's full name */
    "JongWoo Han",
    /* First member's email address */
    "jongwoo0221@gmail.com",
    /* Second member's full name (leave blank if none) */
    "Humyung Lee",
    /* Second member's email address (leave blank if none) */
    "lhm"
};

// CONSTANTS
#define WSIZE 4     // 워드의 크기
#define DSIZE 8     // 더블 워드의 크기       
#define CHUNKSIZE (1<<12)   // 2^12 = 4KB

// MACROS
#define ALIGN(size) (((size) + (0x7) & ~0x7)    // 더블워드 정렬이기 때문에 size보다 큰 8의 배수로 올림
#define MAX(x, y) ((x) > (y) ? (x) : (y))       // x와 y 중 더 큰 값 반환
#define PACK(size, alloc) ((size) | (alloc))    // 블록의 크기와 할당 여부를 pack
#define GET(p) (*(unsigned int *)(p))               // p의 주소의 값 확인
#define PUT(p,val) (*(unsigned int *)(p) = (val))   // p의 주소에 val 값 저장
#define GET_SIZE(p) (GET(p) & ~0x7)             // 블록의 크기 반환
#define GET_ALLOC(p) (GET(p) & 0x1)             // 블록의 할당여부 반환
#define HDRP(bp) ((char *)(bp) - WSIZE)         // 블록의 footer 주소 반환
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)// 블록의 footer 주소 반환
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 이전 블록의 주소 반환
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 다음 블록의 주소 반환
#define PRED_LINK(bp) ((char *)(bp))         // free-list에서 이전 연결 블록 정보
#define SUCC_LINK(bp) ((char *)(bp) + WSIZE) // free-list에서 다음 연결 블록 정보

// PROTOTYPES
static void *extend_heap(size_t words); // 힙의 크기를 늘림
static void *coalesce(void *bp);        // 인접한 가용(free) 블록을 합침
static void *find_fit(size_t asize);    // 가용 리스트(free list) first-fit으로 탐색
static void place(void *bp, size_t asize); // find-fit으로 찾은 블록을 알맞게 위치한다.
void insert_node(char *ptr);            // free()를 통해 가용된 블록을 가용 리스트의 처음 자리에 삽입 (LIFO 정책)
void remove_freenode(char *ptr);        // 가용 리스트의 연결 포인터 수정   

// 
static char *heap_listp = NULL; // 힙의 시작 주소를 가리킴
static char *root = NULL;       // 명시적 가용 리스트의 첫 노드를 가리킴

/*
 * mm_init - 초기힙을 구성하는 함수

 *  _______________________________________                                                             ______________
 * |            |  PROLOGUE  |  PROLOGUE  |                                                            |   EPILOGUE  |
 * |   PADDING  |   HEADER   |   FOOTER   |                                                            |    HEADER   |
 * |------------|------------|------------|-----------|-----------|-----------|    ...    |------------|-------------|
 * |      0     |    8 / 1   |    8 / 1   |   HEADER  |  PREDESOR | SUCCESOR  |    ...    |   FOOTER   |    0 / 1    |
 * |------------|------------|------------|-----------|-----------|-----------|    ...    |------------|-------------|
 * ^                                                                                                                 ^
 * heap_listp                                                                                                      mem_brk
 * root                                                                                                       mem_max_address
 * 
 */
int mm_init(void){
	if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){ //take 4 words from system: then initialize.
		return -1;
	}
	PUT(heap_listp, 0);                             // 미사용 패딩 워드
	PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));    // Prologue Header
	PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));    // Prologue Footer
	PUT(heap_listp + (3*WSIZE), PACK(0, 1));        // Epilogue Header
	root = heap_listp;
    heap_listp += (2 * WSIZE);  // heap_listp는 힙의 시작 위치를 가리키게 한다.
	
	if(extend_heap(CHUNKSIZE / WSIZE) == NULL){ // 초기 힙의 크기를 늘린다.
		return -1;
	}
	
	return 0;
}

/*
 * extend_heap : 힙을 매개변수로 주어진 워드보다 큰 짝수의 크기 바이트만큼 늘린다.
 */

static void *extend_heap(size_t words){
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * DSIZE : words * DSIZE; // 더블워드 정렬을 위해 블록 크기 조정

    if((long)(bp = mem_sbrk(size)) == -1) // size만큼 힙을 늘릴 수 없다면 NULL 반환
        return NULL;

    PUT(HDRP(bp), PACK(size, 0)); // 추가된 free 블록의 header 정보 삽입 
    PUT(FTRP(bp), PACK(size, 0)); // 추가된 free 블록의 footer 정보 삽입
    PUT(SUCC_LINK(bp), 0);        // 추가된 free 블록의 next link 초기화
    PUT(PRED_LINK(bp), 0);        // 추가된 free 블록의 prev link 초기화
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // Epilogue header

    return coalesce(bp);          // 연속된 free 블록 합체
}

/*
 * mm_malloc
 */

void *mm_malloc(size_t size){   
    size_t asize; //adjusted size.
    size_t esize; // if it doesn't fits, extend it to CHUNKSIZE.
    char *bp;
    
	if(size <= 0){
		return NULL;
	} 

    if(size <= DSIZE){
        asize = 2 * DSIZE;
    }
    else{
        asize = DSIZE * ((size + DSIZE + DSIZE-1) / DSIZE); // 더블 워드 정렬을 위해 size보다 크거나 같은 8의 배수로 크기를 재조정
    }
    if((bp = find_fit(asize)) != NULL){ // free-list에서 size보다 큰 리스트 탐색 
        place(bp, asize);
        return bp;
    }

    esize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(esize / DSIZE)) == NULL){ // find_fit을 통해 찾은 free블럭에 배치
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free : ptr에 지정된 블럭에 할당된 메모리 해제 및 free-list에 가용블럭 삽입
 */

void mm_free(void *ptr){
    	
	size_t size = GET_SIZE(HDRP(ptr));
   
    PUT(HDRP(ptr), PACK(size, 0)); // header 및 footer에 할당 정보 0으로 변경
    PUT(FTRP(ptr), PACK(size, 0));
    PUT(SUCC_LINK(ptr), 0);
    PUT(PRED_LINK(ptr), 0);
    coalesce(ptr); // 이전 이후 블럭의 할당 정보를 확인하여 병합하고, free-list에 추가
}

/*
 * coalesce : 현재 bp가 가리키는 블록의 이전 블록과 다음 블록의 할당 여부를 
            확인하여 가용 블럭(free)이 있다면 현재 블록과 인접 가용 블럭을 
            하나의 가용 블럭으로 합친다.
 */
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록의 할당 여부 확인 
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 1 : 할당된 블록, 0 : 가용 블록
    size_t size = GET_SIZE(HDRP(bp)); // 현재 블럭의 크기 확인

    if(prev_alloc && next_alloc){ // CASE 1: 이전, 다음 블럭 모두 할당되어 있으면 합치지 않음
	}
	
    else if(prev_alloc && !next_alloc){ // CASE 2: 다음 블럭이 가용 블럭(free) 일 때
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // 현재 블럭의 크기에 다음 블럭의 크기를 더함
        remove_freenode(NEXT_BLKP(bp));        // 가용 리스트에서 다음 블럭의 연결 제거
        PUT(HDRP(bp), PACK(size, 0));          // 현재 블럭의 header 및 footer에 크기 및 할당 여부 업데이트
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if(!prev_alloc && next_alloc){ // CASE 3: 이전 블럭이 가용 블럭(free) 일 때
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // CASE 2와 유사
        remove_freenode(PREV_BLKP(bp));
        PUT(FTRP(bp),PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)),PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else{  // CASE 4: 이전 블럭 및 다음 블럭 모두 가용 블럭(free) 일 때 
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_freenode(PREV_BLKP(bp));
        remove_freenode(NEXT_BLKP(bp));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert_node(bp); // CASE에 따라 만들어진 블럭을 가용 리스트(free-list)의 가장 앞에 삽입
    return bp;
}

/*
 * mm_free : 
 */

void *mm_realloc(void *ptr, size_t size){
    if(size <= 0){ //equivalent to mm_free(ptr).
        mm_free(ptr);
        return 0;
    }

    if(ptr == NULL){
        return mm_malloc(size); //equivalent to mm_malloc(size).
    }

    void *newp = mm_malloc(size); //new pointer.

    if(newp == NULL){
        return 0;
    }
    
    size_t oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize){
    	oldsize = size; //shrink size.
	} 
    memcpy(newp, ptr, oldsize); //cover.
    mm_free(ptr);
    return newp;
}

/*
 * insert_node : free된 가용 블럭을 free-list의 제일 앞에 삽입한다. 
 */

void insert_node(char *ptr){
    char *SUCC = GET(root);      // 루트가 가리기는 곳의 주소 확인
    if(SUCC != NULL){           // 루트가 없다면
	    PUT(PRED_LINK(SUCC), ptr); // free-list의 이전 블럭 연결을 현재의 블럭으로 연결
	}
    PUT(SUCC_LINK(ptr), SUCC);  // 현재 블럭의 다음 연결을 free-list의 시작 노드로 가리킴 
    PUT(root, ptr); // bp블럭을 루트가 가리키게 한다.
}

/*
 * remove_free : 
 */

void remove_freenode(char *ptr){ 
	if(GET(PRED_LINK(ptr)) == NULL){
		if(GET(SUCC_LINK(ptr)) != NULL){
			PUT(PRED_LINK(GET(SUCC_LINK(ptr))), 0);
		}
		PUT(root, GET(SUCC_LINK(ptr)));
	}
	else{
		if(GET(SUCC_LINK(ptr)) != NULL){
			PUT(PRED_LINK(GET(SUCC_LINK(ptr))), GET(PRED_LINK(ptr)));
		}
		PUT(SUCC_LINK(GET(PRED_LINK(ptr))), GET(SUCC_LINK(ptr)));
	}
	PUT(SUCC_LINK(ptr), 0);
	PUT(PRED_LINK(ptr), 0);
}

/*
 * find_fit : 할당할 블록을 최초 할당 방식으로 찾는 함수
            가용 리스트의 처음부터 마지막부분에 도달할 때까지
            가용 리스트를 탐색하면서 사이즈가 asize보다 크거나 같은 블럭을
            찾으면 그 블럭의 주소를 반환한다. 
            만약 해당 블럭을 찾지 못했다면 NULL을 반환하고 힙을 혹장한다.
 */

static void *find_fit(size_t asize){ 
    char *addr = GET(root);
    while(addr != NULL){
        if(GET_SIZE(HDRP(addr)) >= asize){
        	return addr;
		}
        addr = GET(SUCC_LINK(addr));
    }
    return NULL;
}

/*
 * place : 지정된 크기의 블럭을 find_fit을 통해 찾은 free 블럭에 배치(할당)한다.
        만약 free 블럭에서 동적할당을 받고자하는 블럭의 크기를 제하여도
        또 다른 free블럭을 만들 수 있다면, free 블럭을 쪼갠다.
 */
static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));
    remove_freenode(bp);
    
    // free 블럭에서 동적할당을 받고자하는 블럭의 크기를 제하여도
    // 또 다른 free블럭을 만들 수 있다면, free 블럭을 쪼갠다.
	if((csize - asize) >= (2 * DSIZE)){ 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp); 
        PUT(HDRP(bp), PACK(csize - asize, 0)); // header 및 footer 초기화
        PUT(FTRP(bp), PACK(csize - asize, 0)); 
        PUT(SUCC_LINK(bp), 0);  // 연결 정보 초기화
        PUT(PRED_LINK(bp), 0); 
        coalesce(bp); // 새로 생긴 블럭의 인접 블럭을 확인하여 병합
    }
    else{ // split하더라도 여유롭지 않으므로 그대로 사용
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}