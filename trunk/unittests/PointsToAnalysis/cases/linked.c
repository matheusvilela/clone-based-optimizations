#include <stdio.h>
#include <stdlib.h>

struct test_struct
{
    int val;
    struct test_struct *next;
};

int main() {
  struct test_struct* t0 = (struct test_struct*) malloc (sizeof(struct test_struct));
  struct test_struct* t1 = (struct test_struct*) malloc (sizeof(struct test_struct));
  t0->next = t1;
  t1->next = t0;
}
