//! RUST-M07 demo_types — 컴파일러가 막는 SPSC 오용 5가지. 기본 빌드는 정상 사용 예.
use spsc_m07::Queue;
use std::thread;

fn main() {
    let mut q: Queue<u32, 8> = Queue::new();
    let (mut p, mut c) = q.split();

    #[cfg(feature = "split-twice")]
    let (mut p2, _c2) = q.split(); // 두 번째 생산자를 만들려는 시도

    #[cfg(feature = "clone-producer")]
    let mut p2 = p.clone();

    #[cfg(any(feature = "split-twice", feature = "clone-producer"))]
    p2.enqueue(99).unwrap();

    #[cfg(feature = "shared-producer")]
    {
        let shared = &p; // &Producer 를 두 스레드가 나눠 가지면 생산자가 둘이 된다
        thread::scope(|s| {
            s.spawn(|| shared.enqueue(1));
            s.spawn(|| shared.enqueue(2));
        });
    }

    thread::scope(|s| {
        s.spawn(move || {
            // "UART RX ISR"
            for b in 0..5 {
                p.enqueue(b).unwrap();
            }
        });
    });
    let got: Vec<u32> = std::iter::from_fn(|| c.dequeue()).collect();
    println!("main got {got:?}");

    #[cfg(feature = "outlive")]
    {
        let leaked_producer = {
            let mut local: Queue<u8, 4> = Queue::new();
            local.split().0
        }; // local 은 여기서 사라진다
        drop(leaked_producer);
    }

    #[cfg(feature = "rc-to-isr")]
    {
        let mut rq: Queue<std::rc::Rc<u8>, 4> = Queue::new();
        let (mut rp, _rc) = rq.split();
        thread::scope(|s| {
            s.spawn(move || rp.enqueue(std::rc::Rc::new(1)).ok());
        });
    }
}
