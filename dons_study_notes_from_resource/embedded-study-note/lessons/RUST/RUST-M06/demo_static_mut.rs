//! demo_static_mut.rs — C 식 `static mut` 공유가 Rust 2024 에서 어떻게 거부되는지 rustc 로 직접 본다.
//! (크레이트 없이 std 만 쓰는 단일 파일. 맥에서는 SDKROOT=…/MacOSX26.4.sdk 필요 — md 3장 참고)
//!
//!   rustc --edition 2024 --cfg bad_ref  demo_static_mut.rs   # error: creating a shared reference to mutable static
//!   rustc --edition 2021 --cfg bad_ref  demo_static_mut.rs   # 같은 코드가 2021 에서는 warning 만
//!   rustc --edition 2024 --cfg bad_cell demo_static_mut.rs   # E0277: RefCell<u32> 는 Sync 가 아니다
//!   rustc --edition 2024 --cfg bad_send demo_static_mut.rs   # E0277: Rc 는 Send 가 아니다
//!   rustc --edition 2024 demo_static_mut.rs -o /tmp/RUST-M06_dsm && /tmp/RUST-M06_dsm   # 허용되는 형태
#![allow(dead_code)]

use std::sync::atomic::{AtomicU32, Ordering};

// ---- C 에서 옮겨 온 그대로: ISR 이 올리고 main 이 읽는 전역 ----
static mut COUNTER: u32 = 0;

fn isr_c_style() {
    // 값 자체에 대한 읽기·쓰기(참조를 만들지 않음)는 2024 에서도 컴파일된다 — 여전히 unsafe, 여전히 레이스
    unsafe { COUNTER += 1 };
}

#[cfg(bad_ref)]
fn main_reads_by_reference() -> u32 {
    let r: &u32 = unsafe { &COUNTER }; // 참조: 사는 동안 ISR 이 쓰면 즉시 UB
    *r
}

fn main_reads_by_raw_pointer() -> u32 {
    // 컴파일러가 제안하는 우회로: 참조 대신 raw 포인터. 컴파일은 되지만 안전해진 것은 없다.
    unsafe { (&raw const COUNTER).read_volatile() }
}

// ---- static 은 Sync 여야 한다: ISR 은 "다른 스레드" 와 같은 취급 ----
#[cfg(bad_cell)]
static SHARED: std::cell::RefCell<u32> = std::cell::RefCell::new(0);

#[cfg(bad_send)]
static SLOT: std::sync::Mutex<Option<std::rc::Rc<u8>>> = std::sync::Mutex::new(None);

// ---- 고친 형태 ----
static TICKS: AtomicU32 = AtomicU32::new(0);

fn main() {
    isr_c_style();
    TICKS.fetch_add(1, Ordering::Relaxed);
    #[cfg(bad_ref)]
    println!("by ref: {}", main_reads_by_reference());
    println!("static mut via &raw const: {}", main_reads_by_raw_pointer());
    println!("AtomicU32: {}", TICKS.load(Ordering::Relaxed));
}
