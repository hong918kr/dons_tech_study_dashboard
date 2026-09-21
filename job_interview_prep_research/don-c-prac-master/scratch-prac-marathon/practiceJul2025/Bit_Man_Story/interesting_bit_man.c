/*

https://medium.com/@maityamit/bit-manipulation-basics-for-beginners-concepts-with-all-curated-problems-on-leetcode-b3f25d299329



XOR Property

a^b = c => b^c = a
x^0 = x
x^x = 0


Some Important Property

1 << n = 2^n
get ith set bit number: (1 << i)
get ith Unset bit number: ~(1<<i)

Check ith bit of a Number is Set or Not

if ( ( a & (1<<i) ) !=0  ) cout << "YES";

if( (a & (1<<i)) != 0 ) cout << "Set";


a & 1 == 1 (Odd digit)
a >> 1 -> Divided by 2
a << 1 -> Multiply by 2


Set ith bit of a number => num = num | (1 << i);
UnSet ith bit of a num  => num = num & (~(1<<i));
Check 2^n or not        => (n & (n - 1) == 0); // Yes (2^n)

Set union A | B
Set intersection A & B
Set subtraction A & ~B


*/