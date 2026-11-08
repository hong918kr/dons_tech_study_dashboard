//! demo_race — 스레드 "ISR" 와 main 을 실제로 경쟁시켜 naive 버전의 손실·찢어짐을 센다.
//!   cargo run --release              (호스트 전용: critical-section std 구현)
use rust_m06_solution::{AdcStats, AdcStatsSplit, TickCounter};
use std::hint::spin_loop;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::thread;
use std::time::Instant;

const N: u32 = 2_000_000;

fn gap(spins: u32) {
    for _ in 0..spins {
        spin_loop();
    }
}

/// ① TickCounter: take_naive(load → store 0) vs take(swap)
fn ticks(naive: bool, spins: u32) -> (u64, u32) {
    static T: TickCounter = TickCounter::new();
    T.take();
    let isr = thread::spawn(|| (0..N).for_each(|_| T.isr_tick()));
    let mut got = 0u64;
    let mut polls = 0u32;
    while !isr.is_finished() {
        got += if naive { T.take_naive(|| gap(spins)) } else { T.take() } as u64;
        polls += 1;
    }
    isr.join().unwrap();
    got += T.take() as u64;
    (N as u64 - got, polls)
}

/// ② 통계 창: 필드별 atomic(AdcStatsSplit) vs Mutex<Cell<Window>>(AdcStats)
fn windows(split: bool, spins: u32) -> (u32, u32) {
    static S: AdcStatsSplit = AdcStatsSplit::new();
    static W: AdcStats = AdcStats::new();
    S.take(|| ());
    W.take_window();
    let isr = thread::spawn(move || {
        for seq in 1..=N {
            if split { S.isr_sample(seq) } else { W.isr_sample(seq) }
        }
    });
    // main 은 지금까지 받은 개수로 다음 창의 시작 번호를 안다 → 창의 합이 맞는지 검사
    let (mut next, mut torn, mut wins) = (1u64, 0u32, 0u32);
    let mut check = |c: u32, sum: u64| {
        if c == 0 {
            return;
        }
        wins += 1;
        let c64 = c as u64;
        let expect = c64 * next + c64 * (c64 - 1) / 2;
        let ok = if split { sum as u32 == expect as u32 } else { sum == expect };
        if !ok {
            torn += 1;
        }
        next += c64;
    };
    while !isr.is_finished() {
        if split {
            let (c, s) = S.take(|| gap(spins));
            check(c, s as u64);
        } else {
            let w = W.take_window();
            check(w.count, w.sum);
        }
    }
    isr.join().unwrap();
    if split {
        let (c, s) = S.take(|| ());
        check(c, s as u64);
    } else {
        let w = W.take_window();
        check(w.count, w.sum);
    }
    (torn, wins)
}

/// ③ 호스트에서의 호출당 비용 (경쟁 없음). MCU 비용과 다르다 — 상대 비교용.
fn cost() {
    static A: AtomicU32 = AtomicU32::new(0);
    static F: AtomicBool = AtomicBool::new(false);
    static C: critical_section::Mutex<std::cell::Cell<u64>> =
        critical_section::Mutex::new(std::cell::Cell::new(0));
    const M: u32 = 20_000_000;
    let t = Instant::now();
    for _ in 0..M {
        A.fetch_add(1, Ordering::Relaxed);
    }
    let a = t.elapsed().as_nanos() as f64 / M as f64;
    let t = Instant::now();
    for _ in 0..M {
        F.store(!F.load(Ordering::Acquire), Ordering::Release);
    }
    let f = t.elapsed().as_nanos() as f64 / M as f64;
    let t = Instant::now();
    for _ in 0..M {
        critical_section::with(|cs| {
            let c = C.borrow(cs);
            c.set(c.get() + 1);
        });
    }
    let c = t.elapsed().as_nanos() as f64 / M as f64;
    println!("cost/call (host, uncontended): AtomicU32::fetch_add {a:.2} ns | AtomicBool load+store {f:.2} ns | critical_section::with(std) {c:.2} ns");
    assert_eq!(A.load(Ordering::Relaxed), M);
}

fn main() {
    for spins in [0u32, 50] {
        let (lost, polls) = ticks(true, spins);
        println!("ticks  naive  gap={spins:>2}: lost {lost:>7} of {N} (polls {polls})");
    }
    let (lost, polls) = ticks(false, 0);
    println!("ticks  swap         : lost {lost:>7} of {N} (polls {polls})");
    for spins in [0u32, 50] {
        let (torn, wins) = windows(true, spins);
        println!("window split gap={spins:>2}: torn {torn:>6} of {wins} windows");
    }
    let (torn, wins) = windows(false, 0);
    println!("window cs-Cell      : torn {torn:>6} of {wins} windows");
    cost();
}
