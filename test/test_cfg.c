int test_cfg(int a, int b) {
    int sum = 0;

    if (a > 0) {
        if (b > 0) {
            sum += a + b;
        } else if (b < 0) {
            sum += a - b;
        } else {
            sum += a;
        }
    } else {
        for (int i = 0; i < 3; i++) {
            sum += a + i;
            if (sum > 10) {
                break;
            }
        }
    }

    switch (b % 3) {
        case 0:
            sum += 3;
            break;
        case 1:
            sum += 5;
            break;
        default:
            sum += 7;
    }

    return sum;
}
