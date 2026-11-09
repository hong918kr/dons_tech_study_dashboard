//! loom 모델 검사: 생산자 스레드 1개 + 소비자(메인) 1개의 모든 인터리빙을 C11 메모리 모델로 탐색한다.
//! 실행: RUSTFLAGS="--cfg loom" cargo test --release --test loom_spsc
//! (평소 `cargo test` 에서는 이 파일 전체가 cfg 로 빠진다.)
#![cfg(loom)]

use loom::thread;
use spsc_m07::Queue;

/// 큐는 loom 실행마다 새로 만들고 Box::leak 으로 &'static mut 를 얻는다
/// (loom::thread::spawn 은 'static 클로저만 받는다. 실행당 수백 바이트 누수는 테스트에선 무해).
fn leak<T, const N: usize>() -> &'static mut Queue<T, N> {
    Box::leak(Box::new(Queue::new()))
}

/// N=2 에 3개: 반드시 한 번은 가득 차고(head 핸드오프), 한 번은 슬롯을 재사용한다(wrap).
#[test]
fn loom_three_items_through_two_slots() {
    loom::model(|| {
        let (mut p, mut c) = leak::<u64, 2>().split();
        let isr = thread::spawn(move || {
            for v in 1..=3u64 {
                while p.enqueue(v).is_err() {
                    thread::yield_now();
                }
            }
        });
        let mut got = Vec::new();
        while got.len() < 3 {
            match c.dequeue() {
                Some(v) => got.push(v),
                None => thread::yield_now(),
            }
        }
        isr.join().unwrap();
        assert_eq!(got, [1, 2, 3]);
        assert_eq!(c.dequeue(), None);
    });
}

/// 힙 페이로드(String) + "가득 차면 버리는" ISR: 3개 중 몇 개가 오든 순서·내용이 맞아야 한다.
#[test]
fn loom_string_payload_isr_drops_when_full() {
    loom::model(|| {
        let (mut p, mut c) = leak::<String, 2>().split();
        let isr = thread::spawn(move || {
            let mut sent = Vec::new();
            for i in 0..3 {
                if p.enqueue(format!("rx{i}")).is_ok() {
                    sent.push(i); // ISR 은 기다리지 않는다: 자리가 없으면 버린다
                }
            }
            sent
        });
        let first = loop {
            if let Some(s) = c.dequeue() {
                break s;
            }
            thread::yield_now();
        };
        let sent = isr.join().unwrap();
        let mut got = vec![first];
        while let Some(s) = c.dequeue() {
            got.push(s);
        }
        let want: Vec<String> = sent.iter().map(|i| format!("rx{i}")).collect();
        assert_eq!(got, want);
        assert!(sent.len() >= 2); // 처음 두 개는 빈 큐(N=2)에 반드시 들어간다
    });
}
