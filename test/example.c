void example(int*A, int n, int m) {
  A[m] = 1;
  for (int i = 0; i < n; i++){
    int foo = A[i];
    if(foo > 42) {
      A[i] = 0;
    }
    A[i] += foo + i;
  }
}