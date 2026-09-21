#if 0
// 내장 매크로
#include <stdio.h>

int main() {
	printf("filename = %s\n", __FILE__);
	printf("line = %d\n", __LINE__);
	return 0;
}
#endif

#if 0
// 내장 매크로
#include <stdio.h>
#define TOSTR(x)    #x
int main() {
	printf("filename = %s\n", __FILE__);
	printf("line = %s\n", TOSTR(__LINE__));
	return 0;
}
#endif

#if 0
// 위험한 코드
#include <stdio.h>
#define TOSTR(x)    #x
#define LOG(msg)    printf("["__FILE__"("TOSTR(__LINE__)")] "msg)

int main() {
	LOG("hello, world\n");
	return 0;
}
#endif

#if 0
// 해결 방법
#include <stdio.h>
#define _TOSTR(x)  #x           // 43 => "43"
#define TOSTR(x)    _TOSTR(x)   // __LINE__ => 43
#define LOG(msg)    printf("["__FILE__"("TOSTR(__LINE__)")] "msg)

int main() {
	LOG("hello, world");
	return 0;
}

#endif


#if 0
// ## 매크로
#include <stdio.h>
#define CON(a,b)    a##b           

int main() {
	int xy = 100;
	printf("%d\n", CON(x,y));   // CON(x,y)=>x##y=>xy
	return 0;
}

#endif


#if 1
// ## 매크로
#include <stdio.h>
#define _CON(a,b)    a##b           
#define CON(a,b)    _CON(a,b) 
int main() {
	int x71 = 100;
	printf("%d\n", CON(x, __LINE__));   // CON(x,y)=>x##__LINE__=>x__LINE__
	return 0;
}

#endif