//! RUST-M07 — Lock-free SPSC 큐: ISR 생산자·main 소비자 split
//!
//! 모델 (호스트에서 테스트, 칩에서도 같은 코드):
//!   * "ISR" = 테스트가 띄우는 std::thread (scoped thread). 칩에서는 `#[interrupt] fn USART2()` 가 Producer 를 쥔다.
//!   * `Queue<T, N>`: head/tail = AtomicUsize (free-running, N 은 2의 거듭제곱), 버퍼 = [UnsafeCell<MaybeUninit<T>>; N].
//!   * `split(&mut self)` → (Producer<'_>, Consumer<'_>): &mut 빌림이 살아 있는 동안 두 번째 split 은 컴파일 오류.
//!   * 라이브러리 부분은 #![no_std] 이고 atomic load/store 만 쓴다 → thumbv6m(Cortex-M0+) 에서도 빌드된다.
//!   * `RUSTFLAGS="--cfg loom"` 이면 `sync` 모듈이 loom 타입으로 바뀌고 tests/loom_spsc.rs 가 돈다.
//!
//! Part A — enqueue / dequeue 의 memory ordering (Relaxed 내 인덱스, Acquire 상대 인덱스, Release 게시)
//! Part B — 소유권: Drop 에서 남은 항목 정리, peek, Err(v) 로 값 돌려주기, Send/Sync 근거
//! Part C — 스레드를 ISR 로 삼은 경쟁 테스트 + heapless::spsc 와 비교
//!
//! TODO 표시가 있는 함수 본문만 채운다. 테스트(맨 아래 mod tests)는 starter 와 solution 이 같다.
#![cfg_attr(not(any(test, loom)), no_std)]
// `static Q: Queue<u8, 64> = Queue::new();` 초기화용 const fn new() 로만 만든다.
#![allow(clippy::new_without_default)]
// starter 의 TODO 스텁은 필드·헬퍼를 아직 안 써서 미사용 경고가 난다 → 끈다.
// (solution 은 이 줄 없이도 `cargo clippy --all-targets -- -D warnings` 가 깨끗하다 — 확인함.)
#![allow(dead_code, unused_imports)]

use core::mem::MaybeUninit;

/// 동기화 부품을 한 곳에서 고른다: 평소엔 core, `--cfg loom` 이면 loom 의 모델 타입.
/// (loom 문서가 권하는 "root-level `#[cfg(loom)] mod sync`" 방식)
#[cfg(not(loom))]
mod sync {
    pub use core::sync::atomic::{AtomicUsize, Ordering};

    /// `loom::cell::UnsafeCell` 과 같은 모양(with / with_mut)의 얇은 래퍼.
    /// 코드가 loom 판과 똑같이 생겨야 같은 소스로 두 세계를 다 검사할 수 있다.
    #[repr(transparent)]
    pub struct UnsafeCell<T>(core::cell::UnsafeCell<T>);

    impl<T> UnsafeCell<T> {
        pub const fn new(v: T) -> Self {
            Self(core::cell::UnsafeCell::new(v))
        }
        pub fn with<R>(&self, f: impl FnOnce(*const T) -> R) -> R {
            f(self.0.get())
        }
        pub fn with_mut<R>(&self, f: impl FnOnce(*mut T) -> R) -> R {
            f(self.0.get())
        }
    }
}

#[cfg(loom)]
mod sync {
    pub use loom::cell::UnsafeCell;
    pub use loom::sync::atomic::{AtomicUsize, Ordering};
}

use sync::{AtomicUsize, Ordering, UnsafeCell};

/// head 와 tail 을 서로 다른 캐시 라인에 둔다 (이 맥 `hw.cachelinesize` = 128).
/// Cortex-M4/M0 에는 데이터 캐시가 없으니 칩(target_os = "none")에서는 패딩하지 않는다 — RAM 256 B 절약.
/// 패딩/캐시된 인덱스의 효과 측정은 DS-A01 · LLAT-M01 담당.
#[cfg_attr(not(target_os = "none"), repr(align(128)))]
struct Padded<T>(T);

/// 고정 용량 SPSC 큐. 용량은 **N 개 전부** (heapless 0.9 는 N-1 개).
///
/// 펌웨어: `static mut` 없이 `&'static mut Queue` 를 얻는 방법은 연습 3 (`StaticQueue`) 참고.
///
/// 두 번 split 하면 컴파일 오류 (E0499) — 이것이 "단일 생산자" 를 타입이 보장하는 방식이다:
/// ```compile_fail,E0499
/// let mut q: spsc_m07::Queue<u8, 4> = spsc_m07::Queue::new();
/// let (mut p1, _c1) = q.split();
/// let (mut p2, _c2) = q.split();          // q 는 이미 &mut 로 빌려져 있다
/// p1.enqueue(1).unwrap();
/// p2.enqueue(2).unwrap();
/// ```
/// Producer 는 Clone 이 아니다 (E0599):
/// ```compile_fail,E0599
/// let mut q: spsc_m07::Queue<u8, 4> = spsc_m07::Queue::new();
/// let (p, _c) = q.split();
/// let _p2 = p.clone();
/// ```
/// Producer 가 큐보다 오래 살 수 없다 (E0597):
/// ```compile_fail,E0597
/// let p = {
///     let mut q: spsc_m07::Queue<u8, 4> = spsc_m07::Queue::new();
///     q.split().0
/// };
/// drop(p);
/// ```
/// `T: Send` 가 아니면 Producer 를 다른 스레드(ISR)로 보낼 수 없다 (E0277):
/// ```compile_fail,E0277
/// let mut q: spsc_m07::Queue<std::rc::Rc<u8>, 4> = spsc_m07::Queue::new();
/// let (p, _c) = q.split();
/// std::thread::scope(|s| { s.spawn(move || drop(p)); });
/// ```
pub struct Queue<T, const N: usize> {
    /// 다음에 읽을 위치 — **소비자만 쓴다**, 생산자는 읽기만.
    head: Padded<AtomicUsize>,
    /// 다음에 쓸 위치 — **생산자만 쓴다**, 소비자는 읽기만.
    tail: Padded<AtomicUsize>,
    /// 슬롯 i 의 소유자: head ≤ i < tail 이면 소비자(초기화됨), 아니면 생산자(미초기화).
    buf: [UnsafeCell<MaybeUninit<T>>; N],
}

// SAFETY: &Queue 를 여러 스레드가 가져도 되는 근거.
//   * &Queue 로 할 수 있는 일은 Producer/Consumer 를 통한 접근과 Drop 뿐이다. Producer/Consumer 는
//     split(&mut self) 로만 생기고 각각 하나씩이므로 tail 을 쓰는 쪽·head 를 쓰는 쪽은 언제나 하나다.
//   * 슬롯은 head/tail 이 가리키는 쪽만 만진다. 생산자가 슬롯 t 에 쓰고 tail 을 Release 로 올린 뒤에야
//     소비자가 (Acquire 로 tail 을 보고) 그 슬롯을 읽는다. 소비자가 읽기를 끝내고 head 를 Release 로
//     올린 뒤에야 생산자가 (Acquire 로 head 를 보고) 그 슬롯에 다시 쓴다 → 같은 슬롯 동시 접근 없음.
//   * T 의 값은 스레드 사이를 **이동** 한다 (공유되지 않는다) → T: Send 면 충분, T: Sync 는 필요 없다.
unsafe impl<T: Send, const N: usize> Sync for Queue<T, N> {}
// Send 는 자동: AtomicUsize·UnsafeCell<MaybeUninit<T>> 는 T: Send 면 Send.
// Producer<'a> / Consumer<'a> 는 &'a Queue 를 들고 있으므로 Queue: Sync 이면 자동으로 Send 다.

impl<T, const N: usize> Queue<T, N> {
    const MASK: usize = N - 1;

    /// 빈 큐. `static` 초기화에 쓸 수 있게 const fn.
    #[cfg(not(loom))]
    pub const fn new() -> Self {
        const { assert!(N.is_power_of_two(), "N must be a power of two (free-running index & mask)") };
        Self {
            head: Padded(AtomicUsize::new(0)),
            tail: Padded(AtomicUsize::new(0)),
            buf: [const { UnsafeCell::new(MaybeUninit::uninit()) }; N],
        }
    }

    /// loom 의 AtomicUsize::new / UnsafeCell::new 는 const 가 아니다.
    #[cfg(loom)]
    pub fn new() -> Self {
        Self::with_start_index(0)
    }

    /// (주어짐 — 테스트용) head = tail = `start` 에서 시작. usize 넘침(wrap) 근처를 시험할 때 쓴다.
    #[doc(hidden)]
    pub fn with_start_index(start: usize) -> Self {
        const { assert!(N.is_power_of_two(), "N must be a power of two (free-running index & mask)") };
        Self {
            head: Padded(AtomicUsize::new(start)),
            tail: Padded(AtomicUsize::new(start)),
            buf: core::array::from_fn(|_| UnsafeCell::new(MaybeUninit::uninit())),
        }
    }

    /// 용량 = N (슬롯을 하나도 버리지 않는다).
    pub const fn capacity(&self) -> usize {
        N
    }

    /// (주어짐) 생산자/소비자로 나눈다. `&mut self` 를 빌리므로 둘이 살아 있는 동안 다른 split·접근은 불가.
    pub fn split(&mut self) -> (Producer<'_, T, N>, Consumer<'_, T, N>) {
        let q: &Self = self; // &mut → & 로 재빌림 (공유 참조는 Copy)
        (Producer { q }, Consumer { q })
    }

    /// 들어 있는 항목 수. split 하지 않았을 때만 부를 수 있다 (&self 와 split 의 &mut 는 동시에 못 산다).
    pub fn len(&self) -> usize {
        // TODO(B3): tail - head (wrapping). 이 시점엔 다른 스레드가 없다.
        0
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

impl<T, const N: usize> Drop for Queue<T, N> {
    /// 남은 항목(head..tail)을 정확히 한 번씩 drop 한다. 미초기화 슬롯은 건드리지 않는다.
    fn drop(&mut self) {
        // TODO(B1): head 부터 tail 까지 (wrapping) 각 슬롯의 값을 assume_init_drop
        //           (&mut self 이므로 Relaxed 로 읽어도 된다 — 다른 스레드가 없다)
    }
}

/// 생산자 끝 (ISR 쪽). Clone 이 아니고, 메서드는 `&mut self` → 한 번에 한 곳에서만 enqueue.
pub struct Producer<'a, T, const N: usize> {
    q: &'a Queue<T, N>,
}

/// 소비자 끝 (main 쪽). Clone 이 아니고, dequeue 는 `&mut self`.
pub struct Consumer<'a, T, const N: usize> {
    q: &'a Queue<T, N>,
}

impl<T, const N: usize> Producer<'_, T, N> {
    /// 꼬리에 넣는다. 가득 차 있으면 값을 그대로 `Err(v)` 로 돌려준다 (ISR 은 기다리지 않는다).
    pub fn enqueue(&mut self, v: T) -> Result<(), T> {
        let _q = self.q;
        // TODO(A1): ① 내 인덱스 tail 읽기  ② 상대 인덱스 head 읽기  ③ 가득 참 검사 (tail - head == N)
        //           ④ 슬롯 tail & MASK 에 쓰기  ⑤ tail + 1 게시. 각 load/store 의 Ordering 을 고르고 이유를 주석으로.
        Err(v)
    }

    /// 지금 하나 더 넣을 자리가 있나 (소비자가 비우는 중이면 실제로는 더 많을 수 있다).
    pub fn ready(&self) -> bool {
        self.len() < N
    }

    /// 생산자가 본 항목 수 (상한: 소비자가 그새 꺼냈을 수 있다).
    pub fn len(&self) -> usize {
        // TODO(B3): 내 tail(Relaxed) - 상대 head(Acquire)
        0
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub const fn capacity(&self) -> usize {
        N
    }
}

impl<T, const N: usize> Consumer<'_, T, N> {
    /// 머리에서 꺼낸다. 비었으면 None.
    pub fn dequeue(&mut self) -> Option<T> {
        let _q = self.q;
        // TODO(A2): ① 내 인덱스 head 읽기  ② 상대 인덱스 tail 읽기  ③ 빈 큐 검사
        //           ④ 슬롯 head & MASK 에서 값 꺼내기 (이동)  ⑤ head + 1 게시 (슬롯 반납)
        None
    }

    /// 꺼내지 않고 맨 앞 항목을 본다. 돌려준 &T 가 살아 있는 동안 `&mut self` 인 dequeue 를 못 부르므로
    /// 슬롯이 반납되지 않고, 따라서 생산자가 그 슬롯을 덮어쓸 수 없다.
    pub fn peek(&self) -> Option<&T> {
        // TODO(B2): dequeue 와 같은 검사, 단 head 는 올리지 않는다
        None
    }

    /// 소비자가 본 항목 수 (하한: 생산자가 그새 더 넣었을 수 있다).
    pub fn len(&self) -> usize {
        // TODO(B3): 상대 tail(Acquire) - 내 head(Relaxed)
        0
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub const fn capacity(&self) -> usize {
        N
    }
}

// ============================================================================
// 테스트 하네스 (starter 와 solution 이 같다). loom 빌드에서는 tests/loom_spsc.rs 만 쓴다.
// ============================================================================
#[cfg(all(test, not(loom)))]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::sync::Arc;
    use std::sync::atomic::AtomicUsize as StdAtomicUsize;
    use std::thread;
    use std::time::{Duration, Instant};
    use std::vec::Vec;

    /// TODO 스텁이 영원히 돌지 않게: 모든 대기 루프는 이 시간 안에 끝나야 한다.
    const DEADLINE: Duration = Duration::from_secs(3);

    // ---------------- Part A ----------------
    #[test]
    fn a1_empty_dequeue_is_none() {
        let mut q: Queue<u32, 4> = Queue::new();
        let (_p, mut c) = q.split();
        assert_eq!(c.dequeue(), None);
    }

    #[test]
    fn a2_fifo_order() {
        let mut q: Queue<u32, 8> = Queue::new();
        let (mut p, mut c) = q.split();
        for i in 1..=5 {
            assert_eq!(p.enqueue(i), Ok(()));
        }
        let got: Vec<u32> = core::iter::from_fn(|| c.dequeue()).collect();
        assert_eq!(got, [1, 2, 3, 4, 5]);
    }

    #[test]
    fn a3_capacity_is_n_and_full_returns_value() {
        let mut q: Queue<u32, 4> = Queue::new();
        let (mut p, mut c) = q.split();
        for i in 0..4 {
            assert_eq!(p.enqueue(i), Ok(()), "slot {i}");
        }
        assert_eq!(p.enqueue(99), Err(99)); // 5번째: 값이 그대로 돌아온다
        assert_eq!(c.dequeue(), Some(0));
        assert_eq!(p.enqueue(4), Ok(())); // 한 칸 비면 다시 들어간다
        assert_eq!(p.enqueue(5), Err(5));
    }

    #[test]
    fn a4_interleaved_wraps_buffer_many_times() {
        let mut q: Queue<u64, 4> = Queue::new();
        let (mut p, mut c) = q.split();
        let (mut next_in, mut next_out) = (0u64, 0u64);
        let mut rng = 0x2545_F491u32;
        for _ in 0..10_000 {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            if rng & 1 == 0 {
                if p.enqueue(next_in).is_ok() {
                    next_in += 1;
                }
            } else if let Some(v) = c.dequeue() {
                assert_eq!(v, next_out);
                next_out += 1;
            }
        }
        while let Some(v) = c.dequeue() {
            assert_eq!(v, next_out);
            next_out += 1;
        }
        assert_eq!(next_out, next_in);
        assert!(next_in > 1000, "rounds = {next_in}");
    }

    #[test]
    fn a5_free_running_index_crosses_usize_max() {
        // head = tail = usize::MAX - 2 → 몇 개 넣으면 tail 이 0, 1, 2 ... 로 넘어간다.
        let mut q: Queue<u32, 4> = Queue::with_start_index(usize::MAX - 2);
        let (mut p, mut c) = q.split();
        for round in 0..3u32 {
            for i in 0..4 {
                assert_eq!(p.enqueue(round * 10 + i), Ok(()));
            }
            assert_eq!(p.enqueue(77), Err(77), "full across wrap, round {round}");
            assert_eq!(p.len(), 4);
            for i in 0..4 {
                assert_eq!(c.dequeue(), Some(round * 10 + i));
            }
            assert_eq!(c.dequeue(), None);
        }
    }

    #[test]
    fn a6_len_seen_from_both_ends() {
        let mut q: Queue<u8, 8> = Queue::new();
        let (mut p, mut c) = q.split();
        assert!(p.is_empty() && c.is_empty());
        for b in b"abc" {
            p.enqueue(*b).unwrap();
        }
        assert_eq!((p.len(), c.len()), (3, 3));
        assert!(p.ready());
        c.dequeue();
        assert_eq!((p.len(), c.len()), (2, 2));
        for _ in 0..6 {
            p.enqueue(b'x').unwrap();
        }
        assert!(!p.ready());
        assert_eq!(p.capacity(), 8);
    }

    // ---------------- Part B ----------------
    /// drop 된 횟수를 세는 값. Rc<Cell> 이라 !Send — 단일 스레드 테스트 전용.
    struct Tracked {
        id: u32,
        drops: Rc<Cell<u32>>,
    }
    impl Drop for Tracked {
        fn drop(&mut self) {
            self.drops.set(self.drops.get() + 1);
        }
    }

    #[test]
    fn b1_drop_releases_remaining_items_exactly_once() {
        let drops = Rc::new(Cell::new(0));
        {
            let mut q: Queue<Tracked, 8> = Queue::new();
            let (mut p, mut c) = q.split();
            for id in 0..5 {
                assert!(p.enqueue(Tracked { id, drops: drops.clone() }).is_ok());
            }
            let a = c.dequeue().unwrap(); // 꺼낸 값의 drop 은 호출자 몫
            assert_eq!(a.id, 0);
            drop(a);
            assert_eq!(drops.get(), 1);
        } // 큐 drop → 남은 4개
        assert_eq!(drops.get(), 5);
        assert_eq!(Rc::strong_count(&drops), 1);
    }

    #[test]
    fn b2_drop_with_items_straddling_the_end() {
        let drops = Rc::new(Cell::new(0));
        {
            let mut q: Queue<Tracked, 4> = Queue::with_start_index(usize::MAX - 1);
            let (mut p, mut c) = q.split();
            for id in 0..4 {
                assert!(p.enqueue(Tracked { id, drops: drops.clone() }).is_ok());
            }
            drop(c.dequeue()); // id 0
            assert!(p.enqueue(Tracked { id: 4, drops: drops.clone() }).is_ok());
            assert_eq!(drops.get(), 1);
        } // 남은 1,2,3,4 (인덱스가 usize::MAX 를 넘어 이어짐)
        assert_eq!(drops.get(), 5);
    }

    #[test]
    fn b3_full_queue_hands_value_back_without_drop() {
        let drops = Rc::new(Cell::new(0));
        let mut q: Queue<Tracked, 2> = Queue::new();
        let (mut p, _c) = q.split();
        assert!(p.enqueue(Tracked { id: 1, drops: drops.clone() }).is_ok());
        assert!(p.enqueue(Tracked { id: 2, drops: drops.clone() }).is_ok());
        let back = p.enqueue(Tracked { id: 3, drops: drops.clone() }).unwrap_err();
        assert_eq!(back.id, 3);
        assert_eq!(drops.get(), 0, "Err(v) 는 값을 버리지 않고 돌려준다");
    }

    #[test]
    fn b4_peek_does_not_consume() {
        let mut q: Queue<u16, 4> = Queue::new();
        let (mut p, mut c) = q.split();
        assert_eq!(c.peek(), None);
        p.enqueue(0x1234).unwrap();
        p.enqueue(0x5678).unwrap();
        assert_eq!(c.peek(), Some(&0x1234));
        assert_eq!(c.peek(), Some(&0x1234));
        assert_eq!(c.dequeue(), Some(0x1234));
        assert_eq!(c.peek(), Some(&0x5678));
    }

    #[test]
    fn b5_resplit_after_drop_keeps_state() {
        let mut q: Queue<u8, 4> = Queue::new();
        {
            let (mut p, _c) = q.split();
            p.enqueue(7).unwrap();
            p.enqueue(8).unwrap();
        } // p, c drop → &mut 빌림 끝
        assert_eq!(q.len(), 2);
        let (_p, mut c) = q.split();
        assert_eq!(c.dequeue(), Some(7));
        assert_eq!(c.dequeue(), Some(8));
    }

    #[test]
    fn b6_send_sync_are_what_we_claim() {
        fn send<X: Send>() {}
        fn sync<X: Sync>() {}
        sync::<Queue<u32, 8>>();
        send::<Queue<u32, 8>>();
        send::<Producer<'static, u32, 8>>();
        send::<Consumer<'static, u32, 8>>();
        send::<Producer<'static, Box<[u8; 16]>, 8>>();
        // Rc 같은 !Send 는 compile_fail 문서 테스트(E0277)에서 확인
    }

    // ---------------- Part C ----------------
    /// "ISR" 스레드: seq 0..n 을 넣는다. 가득 차면 기다린다 (테스트에서만 — 실제 ISR 은 버리고 센다, 연습 2).
    fn isr_producer<const N: usize>(mut p: Producer<'_, [u64; 4], N>, n: u64) -> Result<(), &'static str> {
        let start = Instant::now();
        for seq in 0..n {
            // 비원자 다중 워드 페이로드: 찢어지면 체크섬이 틀린다
            let mut msg = [seq, seq.wrapping_mul(0x9E37_79B9_7F4A_7C15), !seq, 0];
            msg[3] = msg[0] ^ msg[1] ^ msg[2];
            while let Err(back) = p.enqueue(msg) {
                msg = back;
                if start.elapsed() > DEADLINE {
                    return Err("producer timeout (queue never accepted)");
                }
                std::hint::spin_loop();
            }
        }
        Ok(())
    }

    #[test]
    fn c1_isr_thread_to_main_order_no_loss_no_dup() {
        const MSGS: u64 = 200_000;
        let mut q: Queue<[u64; 4], 64> = Queue::new();
        let (p, mut c) = q.split();
        thread::scope(|s| {
            let isr = s.spawn(move || isr_producer(p, MSGS)); // scoped thread: Producer<'_> 그대로 이동
            let start = Instant::now();
            let mut expect = 0u64;
            while expect < MSGS {
                match c.dequeue() {
                    Some(m) => {
                        assert_eq!(m[0], expect, "order / loss / duplicate");
                        assert_eq!(m[3], m[0] ^ m[1] ^ m[2], "torn payload at seq {expect}");
                        assert_eq!(m[1], expect.wrapping_mul(0x9E37_79B9_7F4A_7C15));
                        expect += 1;
                    }
                    None => {
                        assert!(start.elapsed() < DEADLINE, "consumer timeout at seq {expect}");
                        std::hint::spin_loop();
                    }
                }
            }
            assert_eq!(c.dequeue(), None);
            assert_eq!(isr.join().unwrap(), Ok(()));
        });
    }

    #[test]
    fn c2_tiny_queue_maximal_contention() {
        // N = 2: 거의 매 메시지가 full/empty 경계 → head/tail 핸드오프가 가장 자주 일어난다
        const MSGS: u64 = 50_000;
        let mut q: Queue<[u64; 4], 2> = Queue::new();
        let (p, mut c) = q.split();
        let mut sum = 0u64;
        thread::scope(|s| {
            let isr = s.spawn(move || isr_producer(p, MSGS));
            let start = Instant::now();
            let mut expect = 0u64;
            while expect < MSGS && start.elapsed() < DEADLINE {
                if let Some(m) = c.dequeue() {
                    assert_eq!(m[0], expect);
                    assert_eq!(m[3], m[0] ^ m[1] ^ m[2]);
                    sum = sum.wrapping_add(m[0]);
                    expect += 1;
                }
            }
            assert_eq!(expect, MSGS, "consumer got {expect} / {MSGS} before deadline");
            assert_eq!(isr.join().unwrap(), Ok(()));
        });
        assert_eq!(sum, MSGS * (MSGS - 1) / 2);
    }

    #[test]
    fn c3_heap_payload_moves_and_leftovers_dropped() {
        // Box 를 스레드 사이로 이동. 끝에 남은 것은 Queue::drop 이 해제. 생성 == 해제 를 센다.
        struct Counted(Box<u64>, Arc<StdAtomicUsize>);
        impl Drop for Counted {
            fn drop(&mut self) {
                self.1.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            }
        }
        const MSGS: u64 = 20_000;
        const TAKE: u64 = 15_000; // 소비자는 일부만 꺼내고 나머지는 큐에 남긴다
        let dropped = Arc::new(StdAtomicUsize::new(0));
        {
            let mut q: Queue<Counted, 8192> = Queue::new();
            let (mut p, mut c) = q.split();
            thread::scope(|s| {
                let d = dropped.clone();
                let isr = s.spawn(move || {
                    let start = Instant::now();
                    for i in 0..MSGS {
                        let mut v = Counted(Box::new(i), d.clone());
                        while let Err(back) = p.enqueue(v) {
                            v = back;
                            if start.elapsed() > DEADLINE {
                                return false;
                            }
                            std::hint::spin_loop();
                        }
                    }
                    true
                });
                let start = Instant::now();
                let mut got = 0u64;
                while got < TAKE && start.elapsed() < DEADLINE {
                    if let Some(v) = c.dequeue() {
                        assert_eq!(*v.0, got);
                        got += 1;
                    }
                }
                assert_eq!(got, TAKE);
                assert!(isr.join().unwrap(), "producer timeout");
            });
            assert_eq!(dropped.load(std::sync::atomic::Ordering::Relaxed), TAKE as usize);
            assert_eq!(q.len(), (MSGS - TAKE) as usize);
        } // q drop → 남은 5,000개 해제
        assert_eq!(dropped.load(std::sync::atomic::Ordering::Relaxed), MSGS as usize);
    }

    #[test]
    fn c4_compare_with_heapless_spsc() {
        // heapless 0.9.3: 같은 N 에서 용량 N-1 (슬롯 하나를 "가득 참" 구분용으로 비워 둔다)
        let mut hq: heapless::spsc::Queue<u8, 8> = heapless::spsc::Queue::new();
        let (mut hp, mut hc) = hq.split();
        let h_accepted = (0..10u8).filter(|&i| hp.enqueue(i).is_ok()).count();
        let mut ours: Queue<u8, 8> = Queue::new();
        let (mut op, mut oc) = ours.split();
        let o_accepted = (0..10u8).filter(|&i| op.enqueue(i).is_ok()).count();
        assert_eq!(h_accepted, 7, "heapless capacity N-1");
        assert_eq!(o_accepted, 8, "ours capacity N");
        // 같은 FIFO 의미
        for i in 0..7u8 {
            assert_eq!(hc.dequeue(), Some(i));
            assert_eq!(oc.dequeue(), Some(i));
        }
        assert_eq!(oc.dequeue(), Some(7));
        assert_eq!((hc.dequeue(), oc.dequeue()), (None, None));
    }
}
