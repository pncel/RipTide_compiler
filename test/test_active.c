int example(int n, int m, int p) {
  int A = 1;
  int mm = -5;

  A += m * p;
  mm *= m;
  A += n+mm*p;

  if (A >= 0){
    return A;
  } else {
    return (A * -1) + mm;
  }
  
}