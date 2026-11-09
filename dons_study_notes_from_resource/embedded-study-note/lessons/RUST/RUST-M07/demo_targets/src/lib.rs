//! RUST-M07 demo_targets — UART RX: ISR 이 넣고 main 이 꺼내는 64바이트 큐를 칩 타깃으로 컴파일해 asm 을 본다.
#![no_std]

use core::sync::atomic::{AtomicUsize, Ordering};
use spsc_m07::{Consumer, Producer};

/// USART2 RX ISR 가 부를 함수 (실제로는 `#[interrupt] fn USART2()` 안에서 `PRODUCER.enqueue(dr)`).
#[unsafe(no_mangle)]
#[inline(never)]
pub fn uart_isr_push(p: &mut Producer<'static, u8, 64>, byte: u8) -> bool {
    p.enqueue(byte).is_ok()
}

/// main 루프가 부를 함수.
#[unsafe(no_mangle)]
#[inline(never)]
pub fn uart_main_pop(c: &mut Consumer<'static, u8, 64>) -> i32 {
    match c.dequeue() {
        Some(b) => b as i32,
        None => -1,
    }
}

// 비교용: 순서만 다른 store/load 네 개
#[unsafe(no_mangle)]
#[inline(never)]
pub fn store_release(a: &AtomicUsize, v: usize) {
    a.store(v, Ordering::Release)
}
#[unsafe(no_mangle)]
#[inline(never)]
pub fn store_relaxed(a: &AtomicUsize, v: usize) {
    a.store(v, Ordering::Relaxed)
}
#[unsafe(no_mangle)]
#[inline(never)]
pub fn load_acquire(a: &AtomicUsize) -> usize {
    a.load(Ordering::Acquire)
}
#[unsafe(no_mangle)]
#[inline(never)]
pub fn load_relaxed(a: &AtomicUsize) -> usize {
    a.load(Ordering::Relaxed)
}
