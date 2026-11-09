//! RUST-M07 추가 연습 — SPSC 큐 확장
//!   연습 1: 배치 enqueue_slice / dequeue_into — 인덱스 게시(Release store)는 배치당 한 번
//!   연습 2: UART RX ISR — 가득 차면 버리고 overrun 을 센다 (ISR 은 기다리지 않는다) + main 의 줄 조립
//!   연습 3: StaticQueue — `static` 하나에서 split 을 딱 한 번 (cortex_m::singleton! / heapless split_const 의 역할)
//!   연습 4: dequeue_with — 큰 프레임을 복사하지 않고 슬롯 안에서 읽은 뒤 반납
#![cfg_attr(not(test), no_std)]
#![allow(clippy::new_without_default)]

use core::cell::UnsafeCell;
use core::mem::MaybeUninit;
use core::sync::atomic::{AtomicUsize, Ordering};

#[cfg_attr(not(target_os = "none"), repr(align(128)))]
struct Padded<T>(T);

/// solution 과 같은 큐 (용량 N, free-running 인덱스, N = 2의 거듭제곱).
pub struct Queue<T, const N: usize> {
    head: Padded<AtomicUsize>,
    tail: Padded<AtomicUsize>,
    buf: [UnsafeCell<MaybeUninit<T>>; N],
}

// SAFETY: solution 과 같음 — 슬롯 소유권이 head/tail 의 Release/Acquire 로 넘어가고 끝점은 하나씩.
unsafe impl<T: Send, const N: usize> Sync for Queue<T, N> {}

impl<T, const N: usize> Queue<T, N> {
    const MASK: usize = N - 1;

    pub const fn new() -> Self {
        const { assert!(N.is_power_of_two()) };
        Self {
            head: Padded(AtomicUsize::new(0)),
            tail: Padded(AtomicUsize::new(0)),
            buf: [const { UnsafeCell::new(MaybeUninit::uninit()) }; N],
        }
    }

    pub fn split(&mut self) -> (Producer<'_, T, N>, Consumer<'_, T, N>) {
        let q: &Self = self;
        (Producer { q }, Consumer { q })
    }

    fn slot(&self, i: usize) -> *mut MaybeUninit<T> {
        self.buf[i & Self::MASK].get()
    }
}

impl<T, const N: usize> Drop for Queue<T, N> {
    fn drop(&mut self) {
        let (mut i, t) = (*self.head.0.get_mut(), *self.tail.0.get_mut());
        while i != t {
            // SAFETY: head..tail 은 초기화된 슬롯
            unsafe { (*self.slot(i)).assume_init_drop() };
            i = i.wrapping_add(1);
        }
    }
}

pub struct Producer<'a, T, const N: usize> {
    q: &'a Queue<T, N>,
}
pub struct Consumer<'a, T, const N: usize> {
    q: &'a Queue<T, N>,
}

impl<T, const N: usize> Producer<'_, T, N> {
    pub fn enqueue(&mut self, v: T) -> Result<(), T> {
        let t = self.q.tail.0.load(Ordering::Relaxed);
        let h = self.q.head.0.load(Ordering::Acquire);
        if t.wrapping_sub(h) == N {
            return Err(v);
        }
        // SAFETY: 생산자 소유 슬롯
        unsafe { self.q.slot(t).write(MaybeUninit::new(v)) };
        self.q.tail.0.store(t.wrapping_add(1), Ordering::Release);
        Ok(())
    }

    /// 연습 1: 넣을 수 있는 만큼(min(빈 칸, items.len())) 넣고 그 개수를 돌려준다. tail 게시는 한 번.
    pub fn enqueue_slice(&mut self, items: &[T]) -> usize
    where
        T: Copy,
    {
        let t = self.q.tail.0.load(Ordering::Relaxed);
        let h = self.q.head.0.load(Ordering::Acquire);
        let free = N - t.wrapping_sub(h);
        let k = free.min(items.len());
        for (j, v) in items[..k].iter().enumerate() {
            // SAFETY: t..t+k 는 모두 생산자 소유 (k ≤ free)
            unsafe { self.q.slot(t.wrapping_add(j)).write(MaybeUninit::new(*v)) };
        }
        if k > 0 {
            // 한 번의 Release 가 앞의 k 개 쓰기를 모두 게시한다
            self.q.tail.0.store(t.wrapping_add(k), Ordering::Release);
        }
        k
    }
}

impl<T, const N: usize> Consumer<'_, T, N> {
    pub fn dequeue(&mut self) -> Option<T> {
        let h = self.q.head.0.load(Ordering::Relaxed);
        let t = self.q.tail.0.load(Ordering::Acquire);
        if h == t {
            return None;
        }
        // SAFETY: 소비자 소유, 초기화됨
        let v = unsafe { (*self.q.slot(h)).assume_init_read() };
        self.q.head.0.store(h.wrapping_add(1), Ordering::Release);
        Some(v)
    }

    /// 연습 1: 최대 out.len() 개를 꺼내 out 앞쪽에 채우고 개수를 돌려준다. head 반납은 한 번.
    pub fn dequeue_into(&mut self, out: &mut [T]) -> usize
    where
        T: Copy,
    {
        let h = self.q.head.0.load(Ordering::Relaxed);
        let t = self.q.tail.0.load(Ordering::Acquire);
        let k = t.wrapping_sub(h).min(out.len());
        for (j, o) in out[..k].iter_mut().enumerate() {
            // SAFETY: h..h+k 는 소비자 소유, 초기화됨
            *o = unsafe { (*self.q.slot(h.wrapping_add(j))).assume_init_read() };
        }
        if k > 0 {
            self.q.head.0.store(h.wrapping_add(k), Ordering::Release);
        }
        k
    }

    /// 연습 4: 맨 앞 항목을 슬롯 안에서 `f(&T)` 로 읽고, 그 자리에서 drop 한 뒤 슬롯을 반납한다 (이동·복사 없음).
    pub fn dequeue_with<R>(&mut self, f: impl FnOnce(&T) -> R) -> Option<R> {
        let h = self.q.head.0.load(Ordering::Relaxed);
        let t = self.q.tail.0.load(Ordering::Acquire);
        if h == t {
            return None;
        }
        let slot = self.q.slot(h);
        // SAFETY: 소비자 소유, 초기화됨. f 가 도는 동안 head 를 안 올렸으니 생산자는 이 슬롯을 못 쓴다.
        let r = f(unsafe { (*slot).assume_init_ref() });
        // SAFETY: 값의 drop 도 head 를 올리기 *전* 에 — 반납 후엔 생산자가 덮어쓸 수 있다.
        unsafe { (*slot).assume_init_drop() };
        self.q.head.0.store(h.wrapping_add(1), Ordering::Release);
        Some(r)
    }
}

// ---------------------------------------------------------------- 연습 2
/// USART RX ISR 쪽: 받은 바이트를 큐에 넣고, 가득 차면 버리고 센다.
pub struct UartIsr<'a, const N: usize> {
    p: Producer<'a, u8, N>,
    pub overruns: u32,
}

impl<'a, const N: usize> UartIsr<'a, N> {
    pub fn new(p: Producer<'a, u8, N>) -> Self {
        Self { p, overruns: 0 }
    }
    /// ISR 본문: 절대 기다리지 않는다.
    pub fn on_byte(&mut self, b: u8) {
        if self.p.enqueue(b).is_err() {
            self.overruns += 1;
        }
    }
}

/// main 쪽: 바이트를 모아 '\n' 에서 한 줄을 돌려준다. 너무 긴 줄은 잘라 `truncated` 를 센다.
pub struct LineReader<'a, const N: usize> {
    c: Consumer<'a, u8, N>,
    buf: [u8; 32],
    len: usize,
    pub bytes: u32,
    pub truncated: u32,
}

impl<'a, const N: usize> LineReader<'a, N> {
    pub fn new(c: Consumer<'a, u8, N>) -> Self {
        Self { c, buf: [0; 32], len: 0, bytes: 0, truncated: 0 }
    }
    /// 큐에 있는 만큼 읽다가 줄이 끝나면 그 줄을 돌려준다.
    pub fn poll(&mut self) -> Option<([u8; 32], usize)> {
        while let Some(b) = self.c.dequeue() {
            self.bytes += 1;
            if b == b'\n' {
                let out = (self.buf, self.len);
                self.len = 0;
                return Some(out);
            }
            if self.len < self.buf.len() {
                self.buf[self.len] = b;
                self.len += 1;
            } else {
                self.truncated += 1;
            }
        }
        None
    }
}

// ---------------------------------------------------------------- 연습 3
/// `static` 에 두는 큐. `take_split` 은 프로그램 전체에서 딱 한 번만 Some 을 돌려준다.
/// 펌웨어: main 의 init 에서 꺼내 Producer<'static> 는 ISR 쪽 셀(RUST-M06 의 Mutex<RefCell<Option<_>>>)로,
/// Consumer<'static> 는 main 루프로 보낸다.
#[cfg(target_has_atomic = "8")]
pub struct StaticQueue<T, const N: usize> {
    taken: core::sync::atomic::AtomicBool,
    q: UnsafeCell<Queue<T, N>>,
}

// SAFETY: 안쪽 Queue 에 대한 &mut 는 taken 의 swap 을 이긴 단 한 호출만 만든다 → 이후 접근은
// Producer/Consumer 를 통해서만 (Queue: Sync 근거와 같음). T: Send 가 필요한 이유도 같다.
#[cfg(target_has_atomic = "8")]
unsafe impl<T: Send, const N: usize> Sync for StaticQueue<T, N> {}

#[cfg(target_has_atomic = "8")]
impl<T, const N: usize> StaticQueue<T, N> {
    pub const fn new() -> Self {
        Self { taken: core::sync::atomic::AtomicBool::new(false), q: UnsafeCell::new(Queue::new()) }
    }

    pub fn take_split(&'static self) -> Option<(Producer<'static, T, N>, Consumer<'static, T, N>)> {
        // swap 이 원자적이라 두 스레드(또는 main 과 ISR)가 동시에 불러도 true 를 받는 쪽은 하나.
        // Relaxed 로 충분: 여기서 게시하는 데이터가 없다 (큐는 const 초기화된 static).
        if self.taken.swap(true, Ordering::Relaxed) {
            return None;
        }
        // SAFETY: 위 swap 을 이긴 유일한 호출 → 이 &'static mut 는 유일하다.
        let q: &'static mut Queue<T, N> = unsafe { &mut *self.q.get() };
        Some(q.split())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::thread;
    use std::time::{Duration, Instant};
    use std::vec::Vec;

    const DEADLINE: Duration = Duration::from_secs(3);

    // ---------- 연습 1 ----------
    #[test]
    fn p1_enqueue_slice_partial_when_nearly_full() {
        let mut q: Queue<u16, 8> = Queue::new();
        let (mut p, mut c) = q.split();
        assert_eq!(p.enqueue_slice(&[1, 2, 3, 4, 5]), 5);
        assert_eq!(p.enqueue_slice(&[6, 7, 8, 9, 10]), 3); // 빈 칸 3개
        assert_eq!(p.enqueue_slice(&[11]), 0);
        let mut out = [0u16; 4];
        assert_eq!(c.dequeue_into(&mut out), 4);
        assert_eq!(out, [1, 2, 3, 4]);
        assert_eq!(p.enqueue_slice(&[9, 10]), 2); // 경계를 넘어 감김
        let mut rest = [0u16; 16];
        let k = c.dequeue_into(&mut rest);
        assert_eq!(&rest[..k], &[5, 6, 7, 8, 9, 10]);
        assert_eq!(c.dequeue_into(&mut rest), 0);
    }

    #[test]
    fn p1_batches_across_threads_keep_order() {
        const MSGS: u32 = 300_000;
        let mut q: Queue<u32, 256> = Queue::new();
        let (mut p, mut c) = q.split();
        thread::scope(|s| {
            s.spawn(move || {
                let start = Instant::now();
                let mut next = 0u32;
                let mut batch = [0u32; 37]; // 일부러 N 과 서로소인 크기
                while next < MSGS && start.elapsed() < DEADLINE {
                    let k = (MSGS - next).min(37) as usize;
                    for (j, b) in batch[..k].iter_mut().enumerate() {
                        *b = next + j as u32;
                    }
                    next += p.enqueue_slice(&batch[..k]) as u32;
                }
            });
            let start = Instant::now();
            let mut expect = 0u32;
            let mut out = [0u32; 50];
            while expect < MSGS && start.elapsed() < DEADLINE {
                let k = c.dequeue_into(&mut out);
                for v in &out[..k] {
                    assert_eq!(*v, expect);
                    expect += 1;
                }
            }
            assert_eq!(expect, MSGS);
        });
    }

    // ---------- 연습 2 ----------
    #[test]
    fn p2_isr_counts_overruns_without_consumer() {
        let mut q: Queue<u8, 8> = Queue::new();
        let (p, c) = q.split();
        let mut isr = UartIsr::new(p);
        for b in b"hello\nworld" {
            isr.on_byte(*b); // 11 바이트, 큐 8칸, main 은 아직 안 읽음
        }
        assert_eq!(isr.overruns, 3);
        let mut rd = LineReader::new(c);
        let (line, n) = rd.poll().unwrap();
        assert_eq!(&line[..n], b"hello");
        assert_eq!(rd.poll(), None); // "wo" 만 남고 줄 끝 없음
        assert_eq!(rd.bytes, 8);
    }

    #[test]
    fn p2_threaded_every_byte_delivered_or_counted() {
        const LINES: u32 = 3_000;
        let mut q: Queue<u8, 64> = Queue::new();
        let (p, c) = q.split();
        let mut sent = 0u32;
        let mut lines_ok = 0u32;
        let mut rd = LineReader::new(c);
        let overruns = thread::scope(|s| {
            let isr = s.spawn(|| {
                let mut isr = UartIsr::new(p);
                let mut sent = 0u32;
                for n in 0..LINES {
                    let mut tmp = [0u8; 16];
                    let len = fmt_line(&mut tmp, n);
                    for b in &tmp[..len] {
                        isr.on_byte(*b);
                        sent += 1;
                    }
                }
                (isr.overruns, sent)
            });
            let start = Instant::now();
            while !isr.is_finished() && start.elapsed() < DEADLINE {
                if let Some((line, n)) = rd.poll()
                    && line[0] == b'L'
                    && n > 1
                {
                    lines_ok += 1;
                }
            }
            let (ov, s_) = isr.join().unwrap();
            sent = s_;
            while let Some((line, n)) = rd.poll() {
                lines_ok += (line[0] == b'L' && n > 1) as u32;
            }
            ov
        });
        // 정확히 맞아야 하는 회계: 받은 바이트 + 버린 바이트 == 보낸 바이트 (마지막 미완성 줄의 바이트도 bytes 에 포함)
        assert_eq!(rd.bytes + overruns, sent);
        assert!(lines_ok <= LINES);
    }

    fn fmt_line(out: &mut [u8; 16], n: u32) -> usize {
        out[0] = b'L';
        let mut digits = [0u8; 10];
        let (mut k, mut v) = (0, n);
        loop {
            digits[k] = b'0' + (v % 10) as u8;
            k += 1;
            v /= 10;
            if v == 0 {
                break;
            }
        }
        for i in 0..k {
            out[1 + i] = digits[k - 1 - i];
        }
        out[1 + k] = b'\n';
        k + 2
    }

    // ---------- 연습 3 ----------
    static RX: StaticQueue<u32, 16> = StaticQueue::new();

    #[test]
    fn p3_static_split_only_once_and_static_threads() {
        let (mut p, mut c) = RX.take_split().expect("first take");
        assert!(RX.take_split().is_none(), "second take must fail");
        // Producer<'static> 이므로 scoped 가 아닌 std::thread::spawn 으로 보낼 수 있다
        let isr = thread::spawn(move || {
            let start = Instant::now();
            for i in 0..10_000u32 {
                let mut v = i;
                while let Err(back) = p.enqueue(v) {
                    v = back;
                    assert!(start.elapsed() < DEADLINE);
                    std::hint::spin_loop();
                }
            }
        });
        let start = Instant::now();
        let mut expect = 0;
        while expect < 10_000 && start.elapsed() < DEADLINE {
            if let Some(v) = c.dequeue() {
                assert_eq!(v, expect);
                expect += 1;
            }
        }
        isr.join().unwrap();
        assert_eq!(expect, 10_000);
    }

    #[test]
    fn p3_concurrent_take_exactly_one_winner() {
        static Q2: StaticQueue<u8, 4> = StaticQueue::new();
        let winners: usize = thread::scope(|s| {
            let hs: Vec<_> = (0..8).map(|_| s.spawn(|| Q2.take_split().is_some() as usize)).collect();
            hs.into_iter().map(|h| h.join().unwrap()).sum()
        });
        assert_eq!(winners, 1);
    }

    // ---------- 연습 4 ----------
    struct Frame {
        data: [u8; 256],
        drops: Rc<Cell<u32>>,
    }
    impl Drop for Frame {
        fn drop(&mut self) {
            self.drops.set(self.drops.get() + 1);
        }
    }

    #[test]
    fn p4_dequeue_with_reads_in_place_and_drops_once() {
        let drops = Rc::new(Cell::new(0));
        {
            let mut q: Queue<Frame, 4> = Queue::new();
            let (mut p, mut c) = q.split();
            for k in 0..3u8 {
                let f = Frame { data: [k; 256], drops: drops.clone() };
                assert!(p.enqueue(f).is_ok());
            }
            let sum = c.dequeue_with(|f| f.data.iter().map(|&b| b as u32).sum::<u32>());
            assert_eq!(sum, Some(0));
            assert_eq!(drops.get(), 1, "읽은 프레임은 슬롯 안에서 drop");
            let first = c.dequeue_with(|f| f.data[0]);
            assert_eq!(first, Some(1));
            assert_eq!(drops.get(), 2);
            let f = Frame { data: [9; 256], drops: drops.clone() };
            assert!(p.enqueue(f).is_ok());
        } // 남은 2개 (data 2, 9)
        assert_eq!(drops.get(), 4);
        assert_eq!(Rc::strong_count(&drops), 1);
    }

    #[test]
    fn p4_dequeue_with_empty_is_none() {
        let mut q: Queue<u64, 2> = Queue::new();
        let (_p, mut c) = q.split();
        assert_eq!(c.dequeue_with(|v| *v), None);
    }
}
