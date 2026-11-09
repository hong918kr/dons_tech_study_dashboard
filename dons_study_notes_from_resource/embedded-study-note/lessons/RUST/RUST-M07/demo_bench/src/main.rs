//! RUST-M07 demo_bench — 생산자 1 + 소비자 1 스레드, u64 메시지 MSGS 개, 용량 1024.
//!   ours      : spsc_m07::Queue<u64, 1024> (용량 1024, head/tail 128 B 패딩)
//!   heapless  : heapless::spsc::Queue<u64, 1025>  … 0.9.3 은 N-1 이라 1025 로 맞춤 (2의 거듭제곱 아님 → % 연산)
//!   heapless2 : heapless::spsc::Queue<u64, 1024> (용량 1023, % 가 & 로 최적화되는 크기)
//!   mutex     : std::sync::Mutex<VecDeque<u64>> (용량 1024 에서 막힘)
//!   channel   : std::sync::mpsc::sync_channel(1024)
//!   cached    : 가설 확인용 — ours 와 같되 "상대 인덱스를 지역 캐시에 두고 막힐 때만 다시 읽기"
//!               (캐시된 인덱스 최적화의 정식 측정은 DS-A01 · LLAT-M01 담당, 여기선 한 줄 확인만)
//! 바쁜 대기(spin_loop) 로 재시도한다. 결과는 Mops/s 중앙값. 다른 프로그램이 돌면 절대값은 흔들린다 → 배수만 본다.
use std::collections::VecDeque;
use std::hint::spin_loop;
use std::cell::UnsafeCell;
use std::mem::MaybeUninit;
use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;
use std::time::Instant;

fn checksum_ok(sum: u64, n: u64) -> bool {
    sum == n * (n - 1) / 2
}

fn run_ours(n: u64) -> f64 {
    let mut q: spsc_m07::Queue<u64, 1024> = spsc_m07::Queue::new();
    let (mut p, mut c) = q.split();
    let t0 = Instant::now();
    let sum = thread::scope(|s| {
        s.spawn(move || {
            for i in 0..n {
                while p.enqueue(i).is_err() {
                    spin_loop();
                }
            }
        });
        let (mut sum, mut got) = (0u64, 0u64);
        while got < n {
            if let Some(v) = c.dequeue() {
                sum += v;
                got += 1;
            } else {
                spin_loop();
            }
        }
        sum
    });
    let dt = t0.elapsed().as_secs_f64();
    assert!(checksum_ok(sum, n));
    n as f64 / dt / 1e6
}

fn run_heapless<const N: usize>(n: u64) -> f64 {
    let mut q: heapless::spsc::Queue<u64, N> = heapless::spsc::Queue::new();
    let (mut p, mut c) = q.split();
    let t0 = Instant::now();
    let sum = thread::scope(|s| {
        s.spawn(move || {
            for i in 0..n {
                while p.enqueue(i).is_err() {
                    spin_loop();
                }
            }
        });
        let (mut sum, mut got) = (0u64, 0u64);
        while got < n {
            if let Some(v) = c.dequeue() {
                sum += v;
                got += 1;
            } else {
                spin_loop();
            }
        }
        sum
    });
    let dt = t0.elapsed().as_secs_f64();
    assert!(checksum_ok(sum, n));
    n as f64 / dt / 1e6
}

fn run_mutex(n: u64) -> f64 {
    let q = Mutex::new(VecDeque::with_capacity(1024));
    let t0 = Instant::now();
    let sum = thread::scope(|s| {
        s.spawn(|| {
            for i in 0..n {
                loop {
                    let mut g = q.lock().unwrap();
                    if g.len() < 1024 {
                        g.push_back(i);
                        break;
                    }
                    drop(g);
                    spin_loop();
                }
            }
        });
        let (mut sum, mut got) = (0u64, 0u64);
        while got < n {
            let v = q.lock().unwrap().pop_front();
            if let Some(v) = v {
                sum += v;
                got += 1;
            } else {
                spin_loop();
            }
        }
        sum
    });
    let dt = t0.elapsed().as_secs_f64();
    assert!(checksum_ok(sum, n));
    n as f64 / dt / 1e6
}

fn run_channel(n: u64) -> f64 {
    let (tx, rx) = std::sync::mpsc::sync_channel::<u64>(1024);
    let t0 = Instant::now();
    let sum = thread::scope(|s| {
        s.spawn(move || {
            for i in 0..n {
                tx.send(i).unwrap();
            }
        });
        let mut sum = 0u64;
        for _ in 0..n {
            sum += rx.recv().unwrap();
        }
        sum
    });
    let dt = t0.elapsed().as_secs_f64();
    assert!(checksum_ok(sum, n));
    n as f64 / dt / 1e6
}

#[repr(align(128))]
struct Line(AtomicUsize);

/// ours 와 같은 순서(Relaxed 내 것 / Acquire 상대 / Release 게시) + 상대 인덱스 지역 캐시.
struct Cached<const N: usize> {
    head: Line,
    tail: Line,
    buf: [UnsafeCell<MaybeUninit<u64>>; N],
}
unsafe impl<const N: usize> Sync for Cached<N> {}

fn run_cached(n: u64) -> f64 {
    const N: usize = 1024;
    let q: Cached<N> = Cached {
        head: Line(AtomicUsize::new(0)),
        tail: Line(AtomicUsize::new(0)),
        buf: [const { UnsafeCell::new(MaybeUninit::uninit()) }; N],
    };
    let q = &q;
    let t0 = Instant::now();
    let sum = thread::scope(|s| {
        s.spawn(move || {
            let (mut t, mut head_cache) = (0usize, 0usize);
            for i in 0..n {
                while t.wrapping_sub(head_cache) == N {
                    head_cache = q.head.0.load(Ordering::Acquire); // 꽉 찼다고 보일 때만 다시 읽는다
                    spin_loop();
                }
                // SAFETY: 생산자 소유 슬롯 (SPSC, 위 검사)
                unsafe { (*q.buf[t & (N - 1)].get()).write(i) };
                t = t.wrapping_add(1);
                q.tail.0.store(t, Ordering::Release);
            }
        });
        let (mut h, mut tail_cache, mut sum, mut got) = (0usize, 0usize, 0u64, 0u64);
        while got < n {
            if h == tail_cache {
                tail_cache = q.tail.0.load(Ordering::Acquire); // 비었다고 보일 때만 다시 읽는다
                if h == tail_cache {
                    spin_loop();
                    continue;
                }
            }
            // SAFETY: 소비자 소유 슬롯, Acquire 로 게시를 받았다
            sum += unsafe { (*q.buf[h & (N - 1)].get()).assume_init_read() };
            h = h.wrapping_add(1);
            q.head.0.store(h, Ordering::Release);
            got += 1;
        }
        sum
    });
    let dt = t0.elapsed().as_secs_f64();
    assert!(checksum_ok(sum, n));
    n as f64 / dt / 1e6
}

fn median(mut v: Vec<f64>) -> f64 {
    v.sort_by(|a, b| a.partial_cmp(b).unwrap());
    v[v.len() / 2]
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: u64 = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(10_000_000);
    let reps: usize = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(5);
    println!("msgs/run = {n}, runs = {reps}, capacity ≈ 1024, u64 payload");
    type Bench = (&'static str, fn(u64) -> f64);
    let benches: [Bench; 6] = [
        ("ours (N=1024)", run_ours),
        ("heapless (N=1025, cap 1024)", run_heapless::<1025>),
        ("heapless (N=1024, cap 1023)", run_heapless::<1024>),
        ("Mutex<VecDeque>", run_mutex),
        ("mpsc::sync_channel(1024)", run_channel),
        ("cached (ours + index cache)", run_cached),
    ];
    // 라운드 로빈: 한 바퀴에 6개를 한 번씩 → 시간에 따라 변하는 부하·코어 배치가 모두에게 고르게 섞인다
    let mut runs: Vec<Vec<f64>> = vec![Vec::new(); benches.len()];
    for _ in 0..reps {
        for (k, (_, f)) in benches.iter().enumerate() {
            runs[k].push(f(n));
        }
    }
    for ((name, _), r) in benches.iter().zip(runs) {
        let lo = r.iter().cloned().fold(f64::MAX, f64::min);
        let hi = r.iter().cloned().fold(0.0, f64::max);
        println!("{name:<30} median {:>8.1} Mops/s   (min {lo:.1}, max {hi:.1})", median(r));
    }
}
