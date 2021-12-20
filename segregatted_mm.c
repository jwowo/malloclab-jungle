/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
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
    "jungle_week6_team10",
    /* First member's full name */
    "Jongwoo Han",
    /* First member's email address */
    "",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};



/* CONSTANTS */
#define WSIZE 4             /* 워드의 크기 */
#define DSIZE 8             /* 더블워드의 크기 (Bytes) */
#define ALIGNMENT 8         /* single word (4) or double word (8) alignment */
#define CHUNKSIZE (1 << 12) /* 힙을 확장할 크기 (2^12, 4KB) */
#define LISTLIMIT 20        /* free list max num */             

/* MACROS */

/* 가장 가까운 더블워드 정렬로 조정 */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

/* sizeof(size_t) = 8, ALIGN(sizeof(size_t)) = 8 */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* 블록의 크기와 할당 여부를 합침 */
#define PACK(size, alloc) ((size) | (alloc))

/* p의 주소의 값을 읽고 쓴다 */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* 비트 연산을 이용한 주소 p가 가리키는 블록의 크기 확인 */
#define GET_SIZE(p) (GET(p) & ~0x7)
// 주소p가 가리키는 블록의 할당 여부 확인 (1: 할당 0: 가용)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* 블록의 시작주소 bp를 통해 블록의 헤더 주소와 풋터 주소 확인 */
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* 현재 블록의 주소를 이용하여 이전, 다음 블록의 주소 확인 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp)-DSIZE)))

/* 이전 가용 블록(Predecessor) 및 다음 가용 블록(Successor)의 포인터 확인 */
#define PRED_FREE(bp) (*(char **)(bp))
#define SUCC_FREE(bp) (*(char **)(bp + WSIZE))

/* PROTOTYPES */
static void *extend_heap(size_t words);         // words 만큼 힙의 크기를 늘림
static void *coalesce(void *bp);                // 인접한 가용 블록(free)을 합침
static void *find_fit(size_t asize);            // 가용 블록 탐색
static void place(void *bp, size_t asize);      // find_fit()으로 찾은 블록을 배치
static void remove_block(void *bp);             // 
static void insert_block(void *bp, size_t size);

static void *heap_listp;                    // 힙의 시작 주소를 가리킴
static void *segregation_list[LISTLIMIT];   // 분리 가용 리스트의 시작 주소를 가리키는 배열 (20개)


/* 
 * mm_init - 힙을 초기화하는 함수
 */
int mm_init(void)
{
    int list;
    
    /* segregated_list 초기화 */ 
    for (list = 0; list < LISTLIMIT; list++) {
        segregation_list[list] = NULL;
    }

    /* 초기에 힙 공간을 16 bytes 만큼 늘린다. 실패시 -1 반환 */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
    {
        return -1;
    }
    PUT(heap_listp, 0);                            /* 미사용 패딩 워드 */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp = heap_listp+2*WSIZE;
    
    /* CHUNSIZE만큼 힙의 크기를 확장하여 초기 가용 블록 생성 */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
    {
        return -1;
    }
    return 0;
}


/* 
 * mm_malloc - 
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int asize = ALIGN(size + SIZE_T_SIZE);
    
    // size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
    {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - 할당되었던 블록을 가용 블럭으로 만든다.
            지정된 블럭에 할당된 메모리 해제 및 segregated-free-list에 가용블럭 삽입한다.
            반환할 블록의 헤더를 통해 해당 블록의 크기를 확인하고
            PACK(size, 0)을 통해 할당 여부에 가용으로 나타낸다.
            free과정은 블록의 데이터를 지우지 않고, 가용 여부만 업데이트 한다.
            그 이유는 이 후 에 기용여부만 확인 후, 다시 할당이 되었을 때 
            새로 데이터의 값을 초기화하면 되기 때문이다. 따로 데이터는 지우지 않는다. 
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));  /* header 및 footer에 할당 정보 0으로 변경 */
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);  /* 이전 이후 블럭의 할당 정보를 확인하여 병합하고, segregated-free-list에 추가 */
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
                따로 수정하지 않음
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/* extend_heap() : 힙이 초기화될 때 혹은 요청한 크기에 맞는 가용 블록을 찾지 못하였을 때 
 *              호출되어 힙의 크기를 확장한다. 
 *              더블워드 정렬을 위해 짝수 개의 WSIZE만큼의 크기로 늘린다.
 *              새로 추가된 힙 영역은 하나의 가용 블럭이므로 
 *              header, footer, epilogue 등의 값을 초기화한다.
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
    {
        return NULL;
    }
    PUT(HDRP(bp), PACK(size, 0));         /* free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue */

    /* 인접 블록의 가용여부를 확인하여 병합가능하면 병합 */
    return coalesce(bp);
}

/*
 * coalesce : 현재 bp가 가리키는 블록의 이전 블록과 다음 블록의 할당 여부를 
 *          확인하여 가용 블럭(free)이 있다면 현재 블록과 인접 가용 블럭을 
 *           하나의 가용 블럭으로 합친다.
 */
static void *coalesce(void *bp)
{
    /* 이전 블록의 할당 여부 확인 (1: 할당, 0: 가용) */
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); 
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp)); /* 현재 블록의 크기 확인 */

    /* Case 1 : 이전, 다음 블럭 모두 할당되어 있으면 합치지 않음 */    
    if (prev_alloc && next_alloc){
    }
    /* Case 2 : 다음 블럭이 가용 블럭(free) 일 때 */
    else if (prev_alloc && !next_alloc)
    { 
        remove_block(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    /* CASE 3: 이전 블럭이 가용 블럭(free) 일 때 */
    else if (!prev_alloc && next_alloc)
    { 
        remove_block(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    /* Case 4 : 이전 블럭 및 다음 블럭 모두 가용 블럭(free) 일 때 */
    else if (!prev_alloc && !next_alloc)
    {
        remove_block(PREV_BLKP(bp));
        remove_block(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    /* CASE에 따라 만들어진 블럭을 분리 가용 리스트(segregated-free-list)에 삽입 */
    insert_block(bp, size); 
    return bp;
}

/*
 * place() : 지정된 크기의 블럭을 find_fit을 통해 찾은 free 블럭에 배치(할당)한다.
 *       만약 free 블럭에서 동적할당을 받고자하는 블럭의 크기를 제하여도
 *      또 다른 free블럭을 만들 수 있다면 free 블럭을 쪼갠다.
 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    remove_block(bp);
    // 필요한 블록 이외에 남는게 16바이트 이상이면 - free header, footer 들어갈 자리 2워드 + payload 2워드?
    if ((csize - asize) >= (2 * DSIZE))
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        coalesce(bp);
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * find_fit() : 할당할 블록을 찾는 함수이다.
 *              사이즈를 비트 연산을 통해 어느 segregation_list부터 
 *              탐색을 해야하는지 먼저 탐색한다.
 *              이 후 해당 segregation_list를 탐색하여   
 *              요청한 크기보다 큰 블록중에서 가장 작은 블록을 탐색한다.
 */
static void *find_fit(size_t asize)
{
    void *bp;

    int list = 0;
    size_t searchsize = asize;

    while (list < LISTLIMIT){
        if ((list == LISTLIMIT-1) || (searchsize <= 1)&&(segregation_list[list] != NULL)){
            bp = segregation_list[list];

            while ((bp != NULL) && (asize > GET_SIZE(HDRP(bp)))){
                bp = SUCC_FREE(bp);
            }
            if (bp != NULL){
                return bp;
            }
        }
        searchsize >>= 1;
        list++;
    }

    return NULL; /* no fit */
}

/*
 *
 */
static void remove_block(void *bp){
    int list = 0;
    size_t size = GET_SIZE(HDRP(bp));

    while ((list < LISTLIMIT - 1) && (size > 1)) {
        size >>= 1;
        list++;
    }

    if (SUCC_FREE(bp) != NULL){
        if (PRED_FREE(bp) != NULL){
            PRED_FREE(SUCC_FREE(bp)) = PRED_FREE(bp);
            SUCC_FREE(PRED_FREE(bp)) = SUCC_FREE(bp);
        }else{
            PRED_FREE(SUCC_FREE(bp)) = NULL;
            segregation_list[list] = SUCC_FREE(bp);
        }
    }else{
        if (PRED_FREE(bp) != NULL){
            SUCC_FREE(PRED_FREE(bp)) = NULL;
        }else{
            segregation_list[list] = NULL;
        }
    }

    return;
}

static void insert_block(void *bp, size_t size){
    int list = 0;
    void *search_ptr;
    void *insert_ptr = NULL;

    while ((list < LISTLIMIT - 1) && (size > 1)){
        size >>=1;
        list++;
    }

    search_ptr = segregation_list[list];
    while ((search_ptr != NULL) && (size > GET_SIZE(HDRP(search_ptr)))){
        insert_ptr = search_ptr;
        search_ptr = SUCC_FREE(search_ptr);
    }
    
    if (search_ptr != NULL){
        if (insert_ptr != NULL){
            SUCC_FREE(bp) = search_ptr;
            PRED_FREE(bp) = insert_ptr;
            PRED_FREE(search_ptr) = bp;
            SUCC_FREE(insert_ptr) = bp;
        }else{
            SUCC_FREE(bp) = search_ptr;
            PRED_FREE(bp) = NULL;
            PRED_FREE(search_ptr) = bp;
            segregation_list[list] = bp;
        }
    }else{
        if (insert_ptr != NULL){
            SUCC_FREE(bp) = NULL;
            PRED_FREE(bp) = insert_ptr;
            SUCC_FREE(insert_ptr) = bp;
        }else{
            SUCC_FREE(bp) = NULL;
            PRED_FREE(bp) = NULL;
            segregation_list[list] = bp;
        }
    }

    return;
}