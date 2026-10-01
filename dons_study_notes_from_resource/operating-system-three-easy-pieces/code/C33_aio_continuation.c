// C33_aio_continuation.c — Ch.33.6~33.7 비동기 I/O(POSIX AIO, macOS 지원) + continuation(수동 스택 관리).
//  - 4MB 임시 파일을 만들고, 1MB 씩 4개의 aio_read() 를 "한꺼번에" 날린다 (즉시 리턴).
//  - 각 요청마다 continuation = { aiocb, 끝나면 써 줄 소켓(sd), 요청 id } 를 표에 저장한다.
//    스레드 버전이라면 read(fd); write(sd); 두 줄이면 끝 — sd 는 스택에 있으니까.
//    이벤트 버전에서는 read 가 끝났을 때 "누구에게 보내야 하지?" 를 continuation 에서 찾아야 한다.
//  - 이벤트 루프는 aio_error() 로 완료를 폴링하면서, 그 사이 "다른 일" 을 몇 번 했는지 센다.
//
// build: cc -Wall -Wextra -O0 code/C33_aio_continuation.c -o .work/bin/C33_aio_continuation
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHUNK (1 << 20)
#define NREQ 4

typedef struct {               // continuation: I/O 가 끝난 뒤 이어서 할 일에 필요한 상태
    struct aiocb cb;
    int sd;                    // 결과를 보낼 "네트워크 소켓"
    int req_id;
    int done;
} cont_t;

int main(void) {
    char path[] = "/tmp/c33_aio_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); return 1; }
    char *data = malloc(CHUNK);
    for (int i = 0; i < NREQ; i++) {                 // 청크 i 는 전부 'A'+i
        memset(data, 'A' + i, CHUNK);
        if (write(fd, data, CHUNK) != CHUNK) { perror("write"); return 1; }
    }
    fsync(fd);

    // 클라이언트 소켓 4개 (socketpair 의 한쪽 = 서버가 쓸 sd, 다른 쪽 = 클라이언트가 읽음)
    int client_end[NREQ];
    cont_t *conts = calloc(NREQ, sizeof(cont_t));
    for (int i = 0; i < NREQ; i++) {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        conts[i].sd = sv[0]; client_end[i] = sv[1];
    }

    // 1) 비동기 읽기 4개를 한꺼번에 발행 — 요청 순서를 일부러 뒤섞음 (3,1,0,2)
    int order[NREQ] = {3, 1, 0, 2};
    for (int j = 0; j < NREQ; j++) {
        int i = order[j];
        cont_t *c = &conts[i];
        c->req_id = i;
        c->cb.aio_fildes = fd;
        c->cb.aio_offset = (off_t)i * CHUNK;
        c->cb.aio_buf = malloc(CHUNK);
        c->cb.aio_nbytes = CHUNK;
        if (aio_read(&c->cb) != 0) { perror("aio_read"); return 1; }
        printf("issued aio_read req%d (offset %dMB) -> returned immediately\n", i, i);
    }

    // 2) 이벤트 루프: 완료 폴링 + 그 사이 다른 일
    long other_work = 0, polls = 0;
    int remaining = NREQ;
    while (remaining > 0) {
        for (int i = 0; i < NREQ; i++) {
            cont_t *c = &conts[i];
            if (c->done) continue;
            polls++;
            int e = aio_error(&c->cb);
            if (e == EINPROGRESS) continue;
            if (e != 0) { fprintf(stderr, "req%d failed: %s\n", i, strerror(e)); return 1; }
            ssize_t n = aio_return(&c->cb);          // 완료된 요청은 반드시 aio_return 으로 회수
            // continuation 실행: 저장해 둔 sd 로 "응답" 의 첫 16바이트를 보낸다
            char *buf = (char *)c->cb.aio_buf;
            ssize_t w = write(c->sd, buf, 16);
            printf("req%d complete: %zd bytes, first byte '%c' -> continuation writes %zd bytes to sd=%d\n",
                   c->req_id, n, buf[0], w, c->sd);
            c->done = 1; remaining--;
        }
        other_work++;                                // 여기서 네트워크 이벤트 처리 등 다른 일을 할 수 있다
    }
    printf("loop iterations doing other work: %ld, aio_error polls: %ld\n", other_work, polls);

    // 3) 클라이언트 쪽에서 받은 것 확인
    for (int i = 0; i < NREQ; i++) {
        char got[17] = {0};
        ssize_t r = read(client_end[i], got, 16);
        printf("client%d received %zd bytes: %.16s\n", i, r, got);
    }
    unlink(path);
    return 0;
}
