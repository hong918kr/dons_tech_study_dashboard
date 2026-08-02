#include <stdio.h>
#include <stdlib.h>
#include "rbtree1_prac.h"

typedef struct {
    itn sid;
    struct rb_node node;
} SAWON;

typedef struct
{
    int sid;
    int color;
}INFO;

void __display( struct rb_node *temp, INFO(*a)[10], int *row, int*col)
{

    SAWON *s;
    int i;
    if (temp == 0) return;
    
    ++*row;
    __display( temp->rb_left, a, row, col);
    s = rb_entry( temp,)

}

void 


