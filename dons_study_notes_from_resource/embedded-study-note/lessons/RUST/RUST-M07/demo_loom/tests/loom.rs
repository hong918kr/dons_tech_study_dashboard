//! 같은 시나리오(solution 의 loom_three_items_through_two_slots)를 약화한 Ordering 으로 돌린다.
#![cfg(loom)]

use loom::sync::Arc;
use loom::thread;
use rust_m07_demo_loom::{Queue, describe};

#[test]
fn three_items_through_two_slots() {
    println!("config: {}", describe());
    loom::model(|| {
        let q = Arc::new(Queue::<2>::new());
        let qp = q.clone();
        let isr = thread::spawn(move || {
            for v in 1..=3u64 {
                while !qp.enqueue(v) {
                    thread::yield_now();
                }
            }
        });
        let mut got = Vec::new();
        while got.len() < 3 {
            match q.dequeue() {
                Some(v) => got.push(v),
                None => thread::yield_now(),
            }
        }
        isr.join().unwrap();
        assert_eq!(got, [1, 2, 3]);
    });
}
