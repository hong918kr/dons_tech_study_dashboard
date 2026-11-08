//! RUST-M06 — ISR 공유 상태: static mut 대신 Mutex<RefCell<..>>
//!
//! 모델 (호스트에서 테스트, 칩에서도 같은 코드):
//!   * "ISR" = 테스트가 띄우는 std::thread. 칩에서는 `#[interrupt] fn USART2()` / `#[exception] fn SysTick()`.
//!   * `critical_section::with` = 호스트에선 critical-section 크레이트의 `std` 구현(전역 std Mutex),
//!     Cortex-M 단일 코어에선 cortex-m 의 `critical-section-single-core` 구현
//!     (PRIMASK 저장 → `cpsid i` → PRIMASK 복원 = C 의 __get_PRIMASK/__disable_irq/__set_PRIMASK).
//!   * 라이브러리 부분은 #![no_std]: `cargo build --target thumbv7em-none-eabihf` 로 확인한다.
//!
//! Part A — atomics: TickCounter(AtomicU32), EventFlag(AtomicBool), Mailbox(값+valid 를 한 워드에)
//! Part B — critical_section::Mutex<RefCell<Option<UartRx>>>: main 이 init 한 드라이버를 ISR 과 공유
//! Part C — Mutex<Cell<Window>>: ADC ISR 이 쌓는 64비트 합 통계 + 스레드를 ISR 로 삼은 경쟁 테스트
//!
//! TODO 표시가 있는 함수 본문만 채운다. 테스트(맨 아래 mod tests)는 starter 와 solution 이 같다.
#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]
// 이 타입들은 `static X: T = T::new();` 초기화용 const fn new() 로만 만든다.
#![allow(clippy::new_without_default)]

use core::cell::{Cell, RefCell};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use critical_section::Mutex;

// ============================================================================
// Part A — 단일 워드는 atomic 하나로 (static mut 없이, unsafe 없이)
// ============================================================================

/// SysTick ISR 가 세고 main 루프가 가져가는 tick 카운터.
/// 펌웨어: `static TICKS: TickCounter = TickCounter::new();`
pub struct TickCounter {
    ticks: AtomicU32,
}

impl TickCounter {
    pub const fn new() -> Self {
        Self { ticks: AtomicU32::new(0) }
    }

    /// ISR: tick 하나 (Cortex-M4 에서는 LDREX/ADDS/STREX 루프가 된다).
    pub fn isr_tick(&self) {
        // TODO(A1): 원자적으로 1 증가
    }

    /// main: 쌓인 tick 을 전부 가져가며 0 으로 — 그 사이에 온 tick 을 잃으면 안 된다.
    pub fn take(&self) -> u32 {
        // TODO(A2): "읽기 + 0 쓰기" 를 RMW 연산 하나로
        0
    }

    /// 아직 안 가져간 tick 수 (관찰용).
    pub fn pending(&self) -> u32 {
        self.ticks.load(Ordering::Relaxed)
    }

    /// (주어짐 — 버그 재현용) load → [between] → store(0). C 의 `n = g_cnt; g_cnt = 0;` 그대로.
    /// 테스트는 `between` 자리에 ISR 을 끼워 넣는다.
    pub fn take_naive(&self, between: impl FnOnce()) -> u32 {
        let n = self.ticks.load(Ordering::Relaxed);
        between();
        self.ticks.store(0, Ordering::Relaxed);
        n
    }
}

/// "버튼 눌림 / DMA 완료" 같은 이벤트 1비트. 여러 번 와도 true 하나 (NVIC 펜딩 비트와 같은 정보량).
pub struct EventFlag {
    flag: AtomicBool,
}

impl EventFlag {
    pub const fn new() -> Self {
        Self { flag: AtomicBool::new(false) }
    }

    /// ISR: 이벤트 발생 (주어짐).
    pub fn isr_set(&self) {
        self.flag.store(true, Ordering::Release);
    }

    /// main: 이벤트가 있었으면 내리면서 true. 처리하는 동안 새로 온 이벤트는 남아 있어야 한다.
    /// 사용: `if FLAG.take() { handle(); }`
    pub fn take(&self) -> bool {
        // TODO(A3): 확인과 내리기를 한 번에
        false
    }

    /// (주어짐 — 버그 재현용) `if flag { handle(); flag = false; }` — handle 중 온 이벤트를 지운다.
    pub fn poll_naive(&self, handle: impl FnOnce()) -> bool {
        if self.flag.load(Ordering::Acquire) {
            handle();
            self.flag.store(false, Ordering::Relaxed);
            true
        } else {
            false
        }
    }
}

/// valid 비트. 값 0 과 "비어 있음" 을 구분한다.
pub const MAILBOX_VALID: u32 = 1 << 31;

/// ADC ISR → main: 최신 샘플(u16) 하나. 값과 valid 비트를 **한 워드** 에 묶어 원자 연산 하나로 주고받는다.
pub struct Mailbox {
    word: AtomicU32,
}

impl Mailbox {
    pub const fn new() -> Self {
        Self { word: AtomicU32::new(0) }
    }

    /// ISR: 새 값을 넣는다. 아직 안 읽힌 값을 덮어썼으면 true (overrun).
    pub fn isr_post(&self, v: u16) -> bool {
        // TODO(A4): valid|v 를 넣으면서 이전 워드를 받아 valid 였는지 본다
        let _ = (&self.word, v);
        false
    }

    /// main: 값이 있으면 꺼내며 비운다.
    pub fn take(&self) -> Option<u16> {
        // TODO(A5): 꺼내기와 비우기를 한 번에
        None
    }
}

/// (주어짐 — 버그 재현용) 값과 플래그를 따로 둔 우편함. C 의 `g_val = v; g_ready = 1;` 그대로.
pub struct MailboxSplit {
    value: AtomicU32,
    ready: AtomicBool,
}

impl MailboxSplit {
    pub const fn new() -> Self {
        Self { value: AtomicU32::new(0), ready: AtomicBool::new(false) }
    }

    pub fn isr_post(&self, v: u16) {
        self.value.store(v as u32, Ordering::Relaxed);
        self.ready.store(true, Ordering::Release);
    }

    /// 값을 읽고 → [between] → 플래그를 내린다. between 에 온 새 값은 영영 안 읽힌다.
    pub fn take(&self, between: impl FnOnce()) -> Option<u16> {
        if !self.ready.load(Ordering::Acquire) {
            return None;
        }
        let v = self.value.load(Ordering::Relaxed) as u16;
        between();
        self.ready.store(false, Ordering::Relaxed);
        Some(v)
    }
}

// ============================================================================
// Part B — 여러 필드짜리 드라이버: critical_section::Mutex<RefCell<Option<UartRx>>>
// ============================================================================

pub const LINE_MAX: usize = 16;

/// 완성된 한 줄 (복사본). 임계 구역 밖으로 들고 나가 파싱한다.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Line {
    buf: [u8; LINE_MAX],
    len: usize,
}

impl Line {
    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }
}

/// UART RX 드라이버 상태 (주어짐). 실제 드라이버는 PAC 의 USART2 핸들(RUST-M03 싱글톤)을 소유하므로
/// main 이 init 한 뒤에야 만들어진다 → static 안에서는 Option 으로 "아직 없음" 을 표현한다.
pub struct UartRx {
    pub baud: u32,
    acc: [u8; LINE_MAX],
    acc_len: usize,
    done: Option<Line>,
    pub bytes: u32,
    pub lines: u32,
    pub overruns: u32,
    pub truncated: u32,
}

impl UartRx {
    pub fn new(baud: u32) -> Self {
        Self {
            baud,
            acc: [0; LINE_MAX],
            acc_len: 0,
            done: None,
            bytes: 0,
            lines: 0,
            overruns: 0,
            truncated: 0,
        }
    }

    /// ISR 안에서 부르는 바이트 처리. '\n' 이면 줄 완성. 안 가져간 줄이 있으면 새 줄이 덮고
    /// overruns += 1 (최신 줄 우선). LINE_MAX 를 넘는 글자는 버리고 그 줄은 truncated += 1.
    pub fn on_byte(&mut self, b: u8) {
        self.bytes += 1;
        if b == b'\n' {
            if self.acc_len > LINE_MAX {
                self.truncated += 1;
                self.acc_len = LINE_MAX;
            }
            if self.done.is_some() {
                self.overruns += 1;
            }
            self.done = Some(Line { buf: self.acc, len: self.acc_len });
            self.lines += 1;
            self.acc_len = 0;
        } else {
            if self.acc_len < LINE_MAX {
                self.acc[self.acc_len] = b;
            }
            self.acc_len += 1; // LINE_MAX 를 넘으면 표시만 (글자는 버림)
        }
    }

    pub fn take_line(&mut self) -> Option<Line> {
        self.done.take()
    }
}

/// 공유 칸 접근 실패 이유.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Access {
    NotInstalled,
    Busy,
}

/// ISR 과 main 이 같이 보는 드라이버 칸. 펌웨어: `static UART: SharedUart = SharedUart::new();`
pub struct SharedUart {
    cell: Mutex<RefCell<Option<UartRx>>>,
    early: AtomicU32, // init 전(또는 uninstall 후) ISR 이 버린 바이트 수
}

impl SharedUart {
    pub const fn new() -> Self {
        Self { cell: Mutex::new(RefCell::new(None)), early: AtomicU32::new(0) }
    }

    /// main: init 끝난 드라이버를 넣는다. 이미 들어 있으면 받은 드라이버를 Err 로 돌려준다.
    pub fn install(&self, d: UartRx) -> Result<(), UartRx> {
        // TODO(B1)
        let _ = (&self.cell, d);
        Ok(())
    }

    /// main: 드라이버를 꺼낸다 (deinit, 저전력 진입 전). 이후 ISR 은 바이트를 버린다.
    pub fn uninstall(&self) -> Option<UartRx> {
        // TODO(B2)
        None
    }

    /// 임계 구역 안에서 드라이버를 &mut 로 빌려 f 를 실행한다. 드라이버가 없으면 None.
    /// 같은 컨텍스트에서 다시 빌리면 RefCell 이 panic 한다 (try_with 참고).
    pub fn with<R>(&self, f: impl FnOnce(&mut UartRx) -> R) -> Option<R> {
        // TODO(B3)
        let _ = f;
        None
    }

    /// with 와 같지만 이미 빌려진 상태(재진입)면 panic 대신 Err(Busy), 없으면 Err(NotInstalled).
    pub fn try_with<R>(&self, f: impl FnOnce(&mut UartRx) -> R) -> Result<R, Access> {
        // TODO(B4)
        let _ = f;
        Err(Access::NotInstalled)
    }

    /// ISR: 바이트 하나. 드라이버가 없으면 early 를 올리고 false.
    pub fn isr_rx(&self, b: u8) -> bool {
        // TODO(B5)
        let _ = b;
        false
    }

    /// main: 완성된 줄 하나를 꺼낸다 (복사본 — 파싱은 임계 구역 밖에서).
    pub fn take_line(&self) -> Option<Line> {
        // TODO(B6)
        None
    }

    pub fn early_bytes(&self) -> u32 {
        self.early.load(Ordering::Relaxed)
    }
}

// ============================================================================
// Part C — Copy 데이터는 Mutex<Cell<T>>: ADC 통계 창 (64비트 합 — M4 에는 AtomicU64 가 없다)
// ============================================================================

/// ADC 변환 완료 ISR 이 쌓는 통계 창. 샘플 번호 seq(1, 2, 3, ...)를 값으로도 쓴다.
/// 불변식: count == last - first + 1, sum == first + ... + last (count > 0 일 때).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Window {
    pub count: u32,
    pub first: u32,
    pub last: u32,
    pub sum: u64,
}

impl Window {
    pub const EMPTY: Window = Window { count: 0, first: 0, last: 0, sum: 0 };

    /// 샘플 하나 추가 (주어짐) — 필드 4개가 함께 바뀐다.
    pub fn push(&mut self, seq: u32) {
        if self.count == 0 {
            self.first = seq;
        }
        self.count += 1;
        self.last = seq;
        self.sum += seq as u64;
    }
}

/// ISR 과 main 이 공유하는 통계. Window 는 Copy 라 RefCell 대신 Cell 로 충분하다.
pub struct AdcStats {
    win: Mutex<Cell<Window>>,
}

impl AdcStats {
    pub const fn new() -> Self {
        Self { win: Mutex::new(Cell::new(Window::EMPTY)) }
    }

    /// ISR: 샘플 하나를 창에 더한다.
    pub fn isr_sample(&self, seq: u32) {
        // TODO(C1): 임계 구역 안에서 get → push → set
        let _ = (&self.win, seq);
    }

    /// main: 지금까지의 창을 통째로 가져가며 비운다.
    pub fn take_window(&self) -> Window {
        // TODO(C2)
        Window::EMPTY
    }
}

/// (주어짐 — 버그 재현용) 필드마다 따로 atomic. 각각은 원자적이지만 둘의 "짝" 은 찢어진다.
pub struct AdcStatsSplit {
    count: AtomicU32,
    sum: AtomicU32, // wrapping 합 (32비트 MCU 라 64비트 atomic 이 없어서)
}

impl AdcStatsSplit {
    pub const fn new() -> Self {
        Self { count: AtomicU32::new(0), sum: AtomicU32::new(0) }
    }

    pub fn isr_sample(&self, seq: u32) {
        self.count.fetch_add(1, Ordering::Relaxed);
        self.sum.fetch_add(seq, Ordering::Relaxed);
    }

    /// (count, sum) 을 가져간다: count 를 swap → [between] → sum 을 swap.
    pub fn take(&self, between: impl FnOnce()) -> (u32, u32) {
        let c = self.count.swap(0, Ordering::Relaxed);
        between();
        let s = self.sum.swap(0, Ordering::Relaxed);
        (c, s)
    }
}

// ============================================================================
// 테스트 (starter 와 동일). 호스트: cargo test  — "ISR" 은 std::thread
// ============================================================================
#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;
    use std::vec::Vec;

    // ---------------- Part A ----------------
    /// 테스트 전용: TODO(A1) 과 무관하게 버그를 재현하려고 필드를 직접 올린다.
    fn raw_tick(t: &TickCounter) {
        t.ticks.fetch_add(1, Ordering::Relaxed);
    }

    #[test]
    fn a1_isr_tick_counts() {
        let t = TickCounter::new();
        t.isr_tick();
        t.isr_tick();
        t.isr_tick();
        assert_eq!(t.pending(), 3);
    }

    #[test]
    fn a2_take_returns_and_zeroes() {
        let t = TickCounter::new();
        for _ in 0..5 {
            t.isr_tick();
        }
        assert_eq!(t.take(), 5);
        assert_eq!(t.pending(), 0);
        assert_eq!(t.take(), 0);
    }

    #[test]
    fn a3_given_take_naive_loses_a_tick() {
        // load 직후 ISR 이 끼어들면: 3 을 가져갔는데 ISR 은 4번 → 4번째 tick 증발
        let t = TickCounter::new();
        for _ in 0..3 {
            raw_tick(&t);
        }
        let n = t.take_naive(|| raw_tick(&t));
        assert_eq!(n, 3);
        assert_eq!(t.pending(), 0); // 4번째가 사라졌다
    }

    #[test]
    fn a4_take_never_loses_ticks() {
        let t = TickCounter::new();
        let mut got = 0;
        for round in 0..10 {
            for _ in 0..round {
                t.isr_tick();
            }
            got += t.take();
        }
        assert_eq!(got, 45);
    }

    #[test]
    fn a5_flag_take_consumes_once() {
        let f = EventFlag::new();
        assert!(!f.take());
        f.isr_set();
        f.isr_set(); // 두 번 와도 하나
        assert!(f.take());
        assert!(!f.take());
    }

    #[test]
    fn a6_given_flag_naive_loses_event_during_handling() {
        let f = EventFlag::new();
        f.isr_set();
        assert!(f.poll_naive(|| f.isr_set())); // 처리 중 새 이벤트
        assert!(!f.poll_naive(|| ())); // 새 이벤트가 지워졌다
    }

    #[test]
    fn a7_flag_event_during_handling_is_kept() {
        let f = EventFlag::new();
        f.isr_set();
        if f.take() {
            f.isr_set(); // handle() 도중 ISR
        }
        assert!(f.take());
    }

    #[test]
    fn a8_mailbox_post_take() {
        let m = Mailbox::new();
        assert_eq!(m.take(), None);
        assert!(!m.isr_post(100));
        assert_eq!(m.take(), Some(100));
        assert_eq!(m.take(), None);
    }

    #[test]
    fn a9_mailbox_overrun_keeps_latest() {
        let m = Mailbox::new();
        assert!(!m.isr_post(1));
        assert!(m.isr_post(2)); // 1 을 덮었다
        assert!(m.isr_post(3));
        assert_eq!(m.take(), Some(3));
        assert!(!m.isr_post(4)); // 비운 뒤라 overrun 아님
    }

    #[test]
    fn a10_mailbox_zero_and_max_values() {
        let m = Mailbox::new();
        m.isr_post(0);
        assert_eq!(m.take(), Some(0)); // 0 도 값이다 (valid 비트로 구분)
        m.isr_post(0xFFFF);
        assert_eq!(m.take(), Some(0xFFFF));
    }

    #[test]
    fn a11_given_split_mailbox_loses_latest_value() {
        let m = MailboxSplit::new();
        m.isr_post(1);
        assert_eq!(m.take(|| m.isr_post(2)), Some(1));
        assert_eq!(m.take(|| ()), None); // 2 는 영영 안 읽힌다
    }

    #[test]
    fn a12_types_are_sync_so_they_can_be_static() {
        fn is_sync<T: Sync>() {}
        is_sync::<TickCounter>();
        is_sync::<EventFlag>();
        is_sync::<Mailbox>();
        is_sync::<SharedUart>();
        is_sync::<AdcStats>();
        static _T: TickCounter = TickCounter::new();
        static _U: SharedUart = SharedUart::new();
        static _S: AdcStats = AdcStats::new();
    }

    // ---------------- Part B ----------------
    fn feed(u: &SharedUart, s: &[u8]) -> usize {
        s.iter().filter(|&&b| u.isr_rx(b)).count()
    }

    #[test]
    fn b1_install_then_isr_bytes_make_a_line() {
        let u = SharedUart::new();
        assert!(u.install(UartRx::new(115_200)).is_ok());
        assert_eq!(feed(&u, b"AT+OK\n"), 6);
        let line = u.take_line().expect("line");
        assert_eq!(line.as_bytes(), b"AT+OK");
        assert_eq!(u.take_line(), None);
    }

    #[test]
    fn b2_install_twice_gives_driver_back() {
        let u = SharedUart::new();
        assert!(u.install(UartRx::new(115_200)).is_ok());
        match u.install(UartRx::new(9_600)) {
            Err(d) => assert_eq!(d.baud, 9_600),
            Ok(()) => panic!("second install must fail"),
        }
        assert_eq!(u.with(|d| d.baud), Some(115_200));
    }

    #[test]
    fn b3_bytes_before_install_are_counted_as_early() {
        let u = SharedUart::new();
        assert_eq!(feed(&u, b"xy"), 0);
        assert_eq!(u.early_bytes(), 2);
        assert_eq!(u.take_line(), None);
    }

    #[test]
    fn b4_uninstall_returns_driver_with_its_state() {
        let u = SharedUart::new();
        u.install(UartRx::new(115_200)).ok();
        feed(&u, b"ab\n");
        let d = u.uninstall().expect("driver");
        assert_eq!((d.bytes, d.lines), (3, 1));
        assert!(!u.isr_rx(b'z'));
        assert_eq!(u.early_bytes(), 1);
        assert!(u.uninstall().is_none());
    }

    #[test]
    fn b5_with_and_try_with_return_values() {
        let u = SharedUart::new();
        assert_eq!(u.with(|d| d.bytes), None);
        assert_eq!(u.try_with(|d| d.bytes), Err(Access::NotInstalled));
        u.install(UartRx::new(115_200)).ok();
        feed(&u, b"1\n2\n");
        assert_eq!(u.with(|d| (d.bytes, d.lines, d.overruns)), Some((4, 2, 1)));
        assert_eq!(u.try_with(|d| d.lines), Ok(2));
    }

    #[test]
    fn b6_try_with_inside_with_is_busy_not_ub() {
        let u = SharedUart::new();
        u.install(UartRx::new(115_200)).ok();
        // 같은 컨텍스트에서 다시 빌리기 (콜백이 공유 칸을 또 건드리는 재진입) → Busy
        assert_eq!(u.with(|_| u.try_with(|_| ())), Some(Err(Access::Busy)));
        assert_eq!(u.try_with(|_| ()), Ok(())); // 빌림이 끝나면 다시 된다
    }

    #[test]
    #[should_panic(expected = "RefCell already borrowed")]
    fn b7_nested_with_panics_instead_of_aliasing() {
        let u = SharedUart::new();
        u.install(UartRx::new(115_200)).ok();
        u.with(|_| {
            u.take_line(); // RefCell already borrowed → panic (C 라면 조용한 aliasing)
        });
    }

    #[test]
    fn b8_given_driver_logic_overrun_and_truncation() {
        let mut d = UartRx::new(115_200);
        for &b in b"first\nsecond\n" {
            d.on_byte(b);
        }
        assert_eq!(d.overruns, 1);
        assert_eq!(d.take_line().unwrap().as_bytes(), b"second");
        for &b in b"0123456789abcdefXYZ\n" {
            d.on_byte(b);
        }
        assert_eq!(d.truncated, 1);
        assert_eq!(d.take_line().unwrap().as_bytes(), b"0123456789abcdef");
    }

    // ---------------- Part C ----------------
    fn check_window(w: &Window) -> bool {
        let n = w.count as u64;
        w.count > 0
            && w.last >= w.first
            && w.last - w.first + 1 == w.count
            && w.sum == (w.first as u64 + w.last as u64) * n / 2
    }

    #[test]
    fn c1_window_in_one_context() {
        let s = AdcStats::new();
        for seq in 1..=5 {
            s.isr_sample(seq);
        }
        let w = s.take_window();
        assert_eq!(w, Window { count: 5, first: 1, last: 5, sum: 15 });
        assert!(check_window(&w));
        assert_eq!(s.take_window(), Window::EMPTY);
    }

    #[test]
    fn c2_given_split_atomics_tear_the_pair() {
        let s = AdcStatsSplit::new();
        for seq in 1..=3 {
            s.isr_sample(seq);
        }
        // count 를 가져간 직후 ISR 이 샘플 4 를 넣으면: count 3 개에 sum 은 1+2+3+4
        let (c, sum) = s.take(|| s.isr_sample(4));
        assert_eq!((c, sum), (3, 10)); // 평균 3.33 — 존재한 적 없는 창
        assert_eq!(s.take(|| ()), (1, 0)); // 다음 창: 샘플 1개에 합 0
    }

    const N: u32 = 100_000;

    #[test]
    fn c3_thread_isr_ticks_none_lost() {
        static T: TickCounter = TickCounter::new();
        let isr = thread::spawn(|| {
            for _ in 0..N {
                T.isr_tick();
            }
        });
        let mut got: u64 = 0;
        let mut polls = 0u32;
        while !isr.is_finished() {
            got += T.take() as u64;
            polls += 1;
        }
        isr.join().unwrap();
        got += T.take() as u64;
        assert!(polls > 0);
        assert_eq!(got, N as u64);
    }

    #[test]
    fn c4_thread_isr_windows_stay_consistent() {
        static S: AdcStats = AdcStats::new();
        let isr = thread::spawn(|| {
            for seq in 1..=N {
                S.isr_sample(seq);
            }
        });
        let mut wins: Vec<Window> = Vec::new();
        while !isr.is_finished() {
            let w = S.take_window();
            if w.count > 0 {
                wins.push(w);
            }
        }
        isr.join().unwrap();
        let w = S.take_window();
        if w.count > 0 {
            wins.push(w);
        }
        assert!(!wins.is_empty());
        assert!(wins.iter().all(check_window), "torn window");
        assert_eq!(wins[0].first, 1);
        assert_eq!(wins[wins.len() - 1].last, N);
        assert!(wins.windows(2).all(|p| p[1].first == p[0].last + 1), "gap/overlap");
        assert_eq!(wins.iter().map(|w| w.count as u64).sum::<u64>(), N as u64);
    }

    #[test]
    fn c5_thread_isr_uart_lines_in_order() {
        static U: SharedUart = SharedUart::new();
        const K: u32 = 3_000;
        assert!(U.install(UartRx::new(1_000_000)).is_ok());
        let isr = thread::spawn(|| {
            for i in 1..=K {
                // "#00042\n" 모양: '#' + 5자리 + '\n'
                let mut b = [b'#', 0, 0, 0, 0, 0, b'\n'];
                let mut v = i;
                for k in (1..=5).rev() {
                    b[k] = b'0' + (v % 10) as u8;
                    v /= 10;
                }
                for &x in &b {
                    U.isr_rx(x);
                }
            }
        });
        let parse = |l: &Line| -> Option<u32> {
            let s = l.as_bytes();
            if s.len() != 6 || s[0] != b'#' {
                return None;
            }
            s[1..].iter().try_fold(0u32, |a, &c| c.is_ascii_digit().then(|| a * 10 + (c - b'0') as u32))
        };
        let mut seen: Vec<u32> = Vec::new();
        let mut bad = 0;
        let mut collect = |l: Line| match parse(&l) {
            Some(v) => seen.push(v),
            None => bad += 1,
        };
        while !isr.is_finished() {
            if let Some(l) = U.take_line() {
                collect(l);
            }
        }
        isr.join().unwrap();
        while let Some(l) = U.take_line() {
            collect(l);
        }
        let (bytes, lines, overruns) = U.with(|d| (d.bytes, d.lines, d.overruns)).unwrap();
        assert_eq!(bad, 0, "garbled line");
        assert_eq!(bytes, K * 7);
        assert_eq!(lines, K);
        assert_eq!(seen.len() as u32 + overruns, K);
        assert!(seen.windows(2).all(|p| p[0] < p[1]), "out of order");
        assert_eq!(seen.last(), Some(&K)); // 최신 줄 우선이라 마지막 줄은 반드시 받는다
        assert_eq!(U.early_bytes(), 0);
    }
}
