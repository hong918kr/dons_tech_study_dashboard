/* a.c — 누군가 buf 를 int 하나로 "선언"했다고 착각하고 씀 (tentative definition) */
int buf;
void a_touch(void) { buf = 0x11223344; }
