//! demo_targets — 같은 ISR 카운터 코드가 Cortex-M4 / M0+ 에서 어떻게 되는지 본다.
//!   cargo build --target thumbv7em-none-eabihf                      # OK
//!   cargo build --target thumbv6m-none-eabi                         # E0599: fetch_add / swap 없음
//!   cargo build --target thumbv6m-none-eabi --features pa-cs        # OK (portable-atomic)
//!   cargo build --target thumbv7em-none-eabihf --features try-u64   # AtomicU64 없음
//!   cargo build --target thumbv6m-none-eabi --features pa-single    # OK (단일 코어 가정, PRIMASK 직접)
//! asm 보기: cargo rustc --release --lib --target <T> [--features ..] -- --emit asm
//!          → $CARGO_TARGET_DIR/<T>/release/deps/rust_m06_demo_targets-*.s
#![no_std]

use core::cell::Cell;
use critical_section::Mutex;

#[cfg(not(any(feature = "pa-cs", feature = "pa-single")))]
use core::sync::atomic::{AtomicU32, Ordering};
#[cfg(any(feature = "pa-cs", feature = "pa-single"))]
use portable_atomic::{AtomicU32, Ordering};

pub static TICKS: AtomicU32 = AtomicU32::new(0);

/// SysTick ISR 에서 부르는 부분
#[unsafe(no_mangle)]
pub extern "C" fn demo_isr_tick() {
    TICKS.fetch_add(1, Ordering::Relaxed);
}

/// main 이 쌓인 tick 을 가져가며 0 으로
#[unsafe(no_mangle)]
pub extern "C" fn demo_take_ticks() -> u32 {
    TICKS.swap(0, Ordering::Relaxed)
}

/// load/store 만 쓰는 함수는 M0 에서도 된다
#[unsafe(no_mangle)]
pub extern "C" fn demo_peek_ticks() -> u32 {
    TICKS.load(Ordering::Relaxed)
}

/// 64비트 카운터: 32비트 MCU 에는 AtomicU64 가 없으니 critical section 으로
pub static UPTIME_US: Mutex<Cell<u64>> = Mutex::new(Cell::new(0));

#[unsafe(no_mangle)]
pub extern "C" fn demo_add_uptime(us: u32) {
    critical_section::with(|cs| {
        let c = UPTIME_US.borrow(cs);
        c.set(c.get() + us as u64);
    });
}

#[cfg(feature = "try-u64")]
pub static BIG: core::sync::atomic::AtomicU64 = core::sync::atomic::AtomicU64::new(0);

/// ADC ISR → main 우편함: 값 먼저, 플래그는 Release 로 (main 은 Acquire 로 읽는다)
pub static SAMPLE: AtomicU32 = AtomicU32::new(0);
pub static READY: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);

#[unsafe(no_mangle)]
pub extern "C" fn demo_publish(v: u32) {
    SAMPLE.store(v, Ordering::Relaxed);
    READY.store(true, core::sync::atomic::Ordering::Release);
}
