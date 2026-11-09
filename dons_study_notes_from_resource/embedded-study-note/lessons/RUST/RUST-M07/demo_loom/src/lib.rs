//! RUST-M07 demo_loom — solution 의 큐를 u64 전용으로 줄이고, 네 개의 Ordering 을 feature 로 바꿔 끼운다.
//! loom 전용 (평소 빌드에는 쓰지 않는다): RUSTFLAGS="--cfg loom" cargo test --release --features weak-tail
#![cfg(loom)]
#![allow(clippy::new_without_default)]

use loom::sync::atomic::{AtomicUsize, Ordering};
use Ordering::{Acquire, Relaxed, Release};

#[cfg(not(feature = "atomic-slots"))]
use loom::cell::UnsafeCell;
#[cfg(feature = "atomic-slots")]
use loom::sync::atomic::AtomicU64;

const WEAK_TAIL_STORE: bool = cfg!(any(feature = "weak-tail", feature = "weak-tail-store"));
const WEAK_TAIL_LOAD: bool = cfg!(any(feature = "weak-tail", feature = "weak-tail-load"));
const WEAK_HEAD: bool = cfg!(feature = "weak-head");

/// 생산자: tail 게시 (정답 Release)
pub const P_TAIL_STORE: Ordering = if WEAK_TAIL_STORE { Relaxed } else { Release };
/// 소비자: tail 읽기 (정답 Acquire)
pub const C_TAIL_LOAD: Ordering = if WEAK_TAIL_LOAD { Relaxed } else { Acquire };
/// 소비자: head 반납 (정답 Release)
pub const C_HEAD_STORE: Ordering = if WEAK_HEAD { Relaxed } else { Release };
/// 생산자: head 읽기 (정답 Acquire)
pub const P_HEAD_LOAD: Ordering = if WEAK_HEAD { Relaxed } else { Acquire };

pub fn describe() -> String {
    format!(
        "P_TAIL_STORE={P_TAIL_STORE:?} C_TAIL_LOAD={C_TAIL_LOAD:?} C_HEAD_STORE={C_HEAD_STORE:?} P_HEAD_LOAD={P_HEAD_LOAD:?} slots={}",
        if cfg!(feature = "atomic-slots") { "AtomicU64(Relaxed)" } else { "UnsafeCell<u64>" }
    )
}

pub struct Queue<const N: usize> {
    head: AtomicUsize,
    tail: AtomicUsize,
    #[cfg(not(feature = "atomic-slots"))]
    buf: [UnsafeCell<u64>; N],
    #[cfg(feature = "atomic-slots")]
    buf: [AtomicU64; N],
}

unsafe impl<const N: usize> Sync for Queue<N> {}

impl<const N: usize> Queue<N> {
    pub fn new() -> Self {
        Self {
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
            #[cfg(not(feature = "atomic-slots"))]
            buf: core::array::from_fn(|_| UnsafeCell::new(0)),
            #[cfg(feature = "atomic-slots")]
            buf: core::array::from_fn(|_| AtomicU64::new(0)),
        }
    }

    pub fn enqueue(&self, v: u64) -> bool {
        let t = self.tail.load(Relaxed);
        let h = self.head.load(P_HEAD_LOAD);
        if t.wrapping_sub(h) == N {
            return false;
        }
        #[cfg(not(feature = "atomic-slots"))]
        self.buf[t % N].with_mut(|p| unsafe { *p = v });
        #[cfg(feature = "atomic-slots")]
        self.buf[t % N].store(v, Relaxed);
        self.tail.store(t.wrapping_add(1), P_TAIL_STORE);
        true
    }

    pub fn dequeue(&self) -> Option<u64> {
        let h = self.head.load(Relaxed);
        let t = self.tail.load(C_TAIL_LOAD);
        if h == t {
            return None;
        }
        #[cfg(not(feature = "atomic-slots"))]
        let v = self.buf[h % N].with(|p| unsafe { *p });
        #[cfg(feature = "atomic-slots")]
        let v = self.buf[h % N].load(Relaxed);
        self.head.store(h.wrapping_add(1), C_HEAD_STORE);
        Some(v)
    }
}
