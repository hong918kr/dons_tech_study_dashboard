

int divider(int a, int b) {
    int res = 0;
    while (a >= b) {
        a = a - b;
        res+=1;
    }

    return res;
}
// a - dividend
// b - divisor
int divider_perf_1(int dividend, int divisor) {
    int res = 0;
    // 10 , 3
    while (dividend >= divisor) {
        int temp = divisor;
        int multiple = 1;
        while (temp + temp <= dividend) {
            temp = temp + temp;
            multiple = multiple + multiple;
        }
        dividend = dividend - temp;
        res = res + multiple;
        
    }
    return res;
}


int divider_perf_2(int dividend, int divisor) {
    int res = 0;
    
    while (dividend >= divisor)
    {
        int temp = divisor;
        int mul = 1;
        while ( (temp << 1) <= dividend) {
            //temp = temp + temp;
            temp = (temp << 1);
            //mul = mul + mul;
            mul = (mul << 1);
        }

        dividend = dividend - temp;
        res = res + mul;
    }
    return res;
}


int main ()
{
    int a = 1000;
    int b = 3;  
    printf("divider res: %d\n", divider(a,b)); // return 3;
    printf("divider_perf1 res: %d\n", divider_perf_1(a,b)); // return 3;
    printf("divider_perf2 res: %d\n", divider_perf_2(a,b)); // return 3;
    

    return 0;
}