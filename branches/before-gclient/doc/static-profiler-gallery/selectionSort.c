#include <stdio.h>
#include <stdlib.h>

void selectionSort(int* a, int n) {
  int i,j;
  int iMin;

  for ( j = 0; j < n-1; j++ ) {

    iMin = j;

    for ( i = j+1; i < n; i++ ) {
      if ( a[i] < a[iMin] ) {
        iMin = i;
      }
    }

    if ( iMin != j ) {
      int temp = a[j];
      a[j]     = a[iMin];
      a[iMin]  = temp;
    }
  }
}

int main() {

  int a[] = {2, 5, 1, 4, 3};
  int n = 5;

  selectionSort(a, n);

  printf("ordered array:\n");
  int i;
  for ( i = 0; i < n; i++ ) {
    printf("%d ", a[i]);
  }
  printf("\n");

}
