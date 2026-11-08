//! RUST-M06 추가 연습 — 한 번에 검증
//!   연습 1  ErrorLatch: Mutex<Cell<u32>> 로 만든 "fetch_or 없는" 에러 비트 래치 (M0 용)
//!   연습 2  IrqShared<T>: Mutex<RefCell<Option<T>>> 를 감싼 제네릭 공유 칸
//!   연습 3  Uptime64: AtomicU64 없이 64비트 µs 시각 — hi-lo-hi 재읽기 (C-M14 Part C 의 Rust 판)
//!   연습 4  CasCounter: cfg(target_has_atomic) 로 고르는 core / portable-atomic (thumbv6m 빌드)
#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]
#![allow(clippy::new_without_default)]

use core::cell::{Cell, RefCell};
use core::sync::atomic::{AtomicU32, Ordering};
use critical_section::Mutex;

// ---------------------------------------------------------------- 연습 1
/// 여러 ISR 이 에러 비트(UART 프레이밍 오류, ADC 오버런, ...)를 OR 로 쌓고 main 이 읽으며 지운다.
/// Cortex-M0 에는 fetch_or(LDREX/STREX) 가 없으니 임계 구역 안에서 read-modify-write.
pub struct ErrorLatch {
    bits: Mutex<Cell<u32>>,
}

impl ErrorLatch {
    pub const fn new() -> Self {
        Self { bits: Mutex::new(Cell::new(0)) }
    }

    /// ISR: 비트를 올린다. 이전 값을 돌려준다 (fetch_or 와 같은 계약).
    pub fn raise(&self, mask: u32) -> u32 {
        critical_section::with(|cs| {
            let c = self.bits.borrow(cs);
            let old = c.get();
            c.set(old | mask);
            old
        })
    }

    /// main: 쌓인 비트를 가져가며 0 으로.
    pub fn take(&self) -> u32 {
        critical_section::with(|cs| self.bits.borrow(cs).replace(0))
    }
}

// ---------------------------------------------------------------- 연습 2
/// 접근 실패 이유.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Access {
    NotInstalled,
    Busy,
}

/// 런타임에 만들어지는 아무 드라이버 T 를 ISR 과 공유하는 칸. static 에 두려면 T: Send.
pub struct IrqShared<T> {
    cell: Mutex<RefCell<Option<T>>>,
}

impl<T> IrqShared<T> {
    pub const fn new() -> Self {
        Self { cell: Mutex::new(RefCell::new(None)) }
    }

    pub fn install(&self, v: T) -> Result<(), T> {
        critical_section::with(|cs| {
            let mut slot = self.cell.borrow_ref_mut(cs);
            if slot.is_some() {
                return Err(v);
            }
            *slot = Some(v);
            Ok(())
        })
    }

    pub fn take(&self) -> Option<T> {
        critical_section::with(|cs| self.cell.borrow_ref_mut(cs).take())
    }

    pub fn try_with<R>(&self, f: impl FnOnce(&mut T) -> R) -> Result<R, Access> {
        critical_section::with(|cs| {
            let mut slot = self.cell.borrow(cs).try_borrow_mut().map_err(|_| Access::Busy)?;
            slot.as_mut().map(f).ok_or(Access::NotInstalled)
        })
    }
}

// ---------------------------------------------------------------- 연습 3
/// 1 ms ISR 가 µs 시각을 lo/hi 두 워드로 올린다. 읽기/쓰기는 load/store 뿐이라 M0 에서도 된다.
pub struct Uptime64 {
    hi: AtomicU32,
    lo: AtomicU32,
}

impl Uptime64 {
    pub const fn new(start: u64) -> Self {
        Self { hi: AtomicU32::new((start >> 32) as u32), lo: AtomicU32::new(start as u32) }
    }

    /// ISR: us 만큼 더한다 (lo 먼저, 자리올림이면 hi). ISR 은 main 에게 선점되지 않는다.
    pub fn isr_add(&self, us: u32) {
        let lo = self.lo.load(Ordering::Relaxed);
        let (new_lo, carry) = lo.overflowing_add(us);
        self.lo.store(new_lo, Ordering::Relaxed);
        if carry {
            self.hi.store(self.hi.load(Ordering::Relaxed).wrapping_add(1), Ordering::Relaxed);
        }
    }

    /// (버그 재현용) hi → [after_hi] → lo. 사이에 자리올림 ISR 이 오면 옛 hi + 새 lo.
    pub fn read_naive(&self, after_hi: impl FnOnce()) -> u64 {
        let hi = self.hi.load(Ordering::Acquire);
        after_hi();
        let lo = self.lo.load(Ordering::Acquire);
        ((hi as u64) << 32) | lo as u64
    }

    /// hi → lo → hi, 두 hi 가 같을 때까지. `inject(k)` 는 k 번째 시도의 hi 직후에 불린다 (테스트용 ISR 주입).
    /// 돌려주는 값: (시각, 시도 횟수).
    pub fn read(&self, mut inject: impl FnMut(u32)) -> (u64, u32) {
        let mut tries = 0;
        loop {
            tries += 1;
            let h1 = self.hi.load(Ordering::Acquire); // Acquire: 뒤의 lo 읽기가 앞으로 못 온다
            inject(tries);
            let lo = self.lo.load(Ordering::Acquire); // Acquire: 뒤의 h2 읽기가 앞으로 못 온다
            let h2 = self.hi.load(Ordering::Relaxed);
            if h1 == h2 {
                return ((h1 as u64) << 32 | lo as u64, tries);
            }
        }
    }
}

// ---------------------------------------------------------------- 연습 4
#[cfg(target_has_atomic = "32")]
use core::sync::atomic::AtomicU32 as CasU32;
#[cfg(not(target_has_atomic = "32"))]
use portable_atomic::AtomicU32 as CasU32;

/// M4/M33/호스트: core 의 LDREX/STREX (또는 LSE) 원자. M0/M0+: portable-atomic 이 임계 구역으로 대신.
pub struct CasCounter {
    n: CasU32,
}

impl CasCounter {
    pub const fn new() -> Self {
        Self { n: CasU32::new(0) }
    }

    pub fn isr_inc(&self) {
        self.n.fetch_add(1, Ordering::Relaxed);
    }

    pub fn take(&self) -> u32 {
        self.n.swap(0, Ordering::Relaxed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    // 연습 1
    #[test]
    fn p1_latch_or_and_take() {
        let e = ErrorLatch::new();
        assert_eq!(e.raise(0b001), 0);
        assert_eq!(e.raise(0b100), 0b001);
        assert_eq!(e.raise(0b001), 0b101); // 같은 비트 두 번 = 한 번
        assert_eq!(e.take(), 0b101);
        assert_eq!(e.take(), 0);
    }

    #[test]
    fn p1_latch_two_isr_threads_no_bit_lost() {
        static E: ErrorLatch = ErrorLatch::new();
        // ISR 두 개(서로 다른 우선순위의 IRQ)가 각자 16비트씩 올린다. main 은 계속 take.
        let a = thread::spawn(|| (0..16).for_each(|k| {
            E.raise(1 << k);
        }));
        let b = thread::spawn(|| (16..32).for_each(|k| {
            E.raise(1 << k);
        }));
        let mut seen = 0u32;
        let mut dup = false;
        while !(a.is_finished() && b.is_finished()) {
            let t = E.take();
            dup |= seen & t != 0;
            seen |= t;
        }
        a.join().unwrap();
        b.join().unwrap();
        let t = E.take();
        dup |= seen & t != 0;
        seen |= t;
        assert_eq!(seen, u32::MAX);
        assert!(!dup, "a bit was delivered twice");
    }

    // 연습 2
    struct Motor {
        rpm: u32,
    }

    #[test]
    fn p2_generic_cell_install_with_take() {
        static M: IrqShared<Motor> = IrqShared::new();
        assert_eq!(M.try_with(|m| m.rpm), Err(Access::NotInstalled));
        assert!(M.install(Motor { rpm: 0 }).is_ok());
        assert!(M.install(Motor { rpm: 1 }).is_err());
        assert_eq!(M.try_with(|m| { m.rpm += 1500; m.rpm }), Ok(1500));
        assert_eq!(M.try_with(|_| M.try_with(|_| ())), Ok(Err(Access::Busy)));
        assert_eq!(M.take().map(|m| m.rpm), Some(1500));
        assert!(M.take().is_none());
    }

    #[test]
    fn p2_generic_cell_thread_isr() {
        static C: IrqShared<u64> = IrqShared::new();
        C.install(0).unwrap();
        let isr = thread::spawn(|| {
            for _ in 0..50_000 {
                C.try_with(|v| *v += 1).unwrap();
            }
        });
        let mut last = 0;
        while !isr.is_finished() {
            let v = C.try_with(|v| *v).unwrap();
            assert!(v >= last);
            last = v;
        }
        isr.join().unwrap();
        assert_eq!(C.take(), Some(50_000));
    }

    // 연습 3
    const NEAR_WRAP: u64 = 0x0000_0000_FFFF_FC18; // lo = 2^32 - 1000

    #[test]
    fn p3_naive_read_tears() {
        let t = Uptime64::new(NEAR_WRAP);
        let v = t.read_naive(|| t.isr_add(1000)); // hi 읽은 직후 자리올림
        assert_eq!(v, 0x0000_0000_0000_0000); // 옛 hi(0) + 새 lo(0): 71.6분 뒤로
    }

    #[test]
    fn p3_reread_retries_and_is_correct() {
        let t = Uptime64::new(NEAR_WRAP);
        let (v, tries) = t.read(|k| {
            if k == 1 {
                t.isr_add(1000)
            }
        });
        assert_eq!(v, 0x0000_0001_0000_0000);
        assert_eq!(tries, 2);
        let (v2, tries2) = t.read(|_| ());
        assert_eq!((v2, tries2), (0x0000_0001_0000_0000, 1));
    }

    #[test]
    fn p3_reread_every_injection_point_is_monotonic() {
        // 매 시도마다 ISR 을 한 번씩 끼워 넣어도(두 번째 시도부터는 없음) 값은 항상 실제 시각 중 하나
        for start in [NEAR_WRAP - 3000, NEAR_WRAP - 1000, NEAR_WRAP, NEAR_WRAP + 1000] {
            let t = Uptime64::new(start);
            let (v, _) = t.read(|k| {
                if k == 1 {
                    t.isr_add(1000)
                }
            });
            assert!(v == start || v == start + 1000, "start={start:#x} v={v:#x}");
        }
    }

    // 연습 4
    #[test]
    fn p4_cas_counter_thread_isr() {
        static K: CasCounter = CasCounter::new();
        let isr = thread::spawn(|| (0..100_000).for_each(|_| K.isr_inc()));
        let mut got = 0u64;
        while !isr.is_finished() {
            got += K.take() as u64;
        }
        isr.join().unwrap();
        got += K.take() as u64;
        assert_eq!(got, 100_000);
    }
}
