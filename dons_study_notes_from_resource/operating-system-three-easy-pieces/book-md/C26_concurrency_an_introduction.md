# 26. Concurrency: An Introduction

> 원문: *Operating Systems: Three Easy Pieces* (v0.91) — Part **Concurrency**, PDF p.284–298 · [PDF 원본에서 보기 (그림 포함)](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=284)

> ⚠️ PDF 텍스트 자동 추출본. 그림·표는 깨질 수 있으니 그림이 나오면 PDF 링크로 확인.

Thus far, we have seen the development of the basic abstractions that the OS performs. We have seen how to take a single physical CPU and turn it into multiple virtual CPUs, thus enabling the illusion of multiple programs running at the same time. We have also seen how to create the illusion of a large, private virtual memory for each process; this abstraction of the address space enables each program to behave as if it has its own memory when indeed the OS is secretly multiplexing address spaces across physical memory (and sometimes, disk).

In this note, we introduce a new abstraction for a single running process: that of a thread. Instead of our classic view of a single point of execution within a program (i.e., a single PC where instructions are being fetched from and executed), a multi-threaded program has more than one point of execution (i.e., multiple PCs, each of which is being fetched and executed from). Perhaps another way to think of this is that each thread is very much like a separate process, except for one difference: they share the same address space and thus can access the same data.

The state of a single thread is thus very similar to that of a process. It has a program counter (PC) that tracks where the program is fetching instructions from. Each thread has its own private set of registers it uses for computation; thus, if there are two threads that are running on a single processor, when switching from running one (T1) to running the other (T2), a context switch must take place. The context switch between threads is quite similar to the context switch between processes, as the register state of T1 must be saved and the register state of T2 restored before running T2. With processes, we saved state to a process control block (PCB); now, we’ll need one or more thread control blocks (TCBs) to store the state of each thread of a process. There is one major difference, though, in the context switch we perform between threads as compared to processes: the address space remains the same (i.e., there is no need to switch which page table we are using).

One other major difference between threads and processes concerns the stack. In our simple model of the address space of a classic process (which we can now call a single-threaded process), there is a single stack, usually residing at the bottom of the address space (Figure 26.1, left).

0KB                                               0KB

the code segment:

```text
           Program Code   where instructions live           Program Code
   1KB                                               1KB
                             the heap segment:
               Heap        contains malloc’d data              Heap
                          dynamic data structures
   2KB                      (it grows downward)      2KB
```

(free)

(free)

Stack (2)

(it grows upward)                 (free)

the stack segment:

```text
   15KB                   contains local variables   15KB
                          arguments to routines,
              Stack          return values, etc.              Stack (1)
   16KB                                              16KB
```
*Figure 26.1: Single-Threaded And Multi-Threaded Address Spaces*

However, in a multi-threaded process, each thread runs independently and of course may call into various routines to do whatever work it is doing. Instead of a single stack in the address space, there will be one per thread. Let’s say we have a multi-threaded process that has two threads in it; the resulting address space looks different (Figure 26.1, right).

In this figure, you can see two stacks spread throughout the address space of the process. Thus, any stack-allocated variables, parameters, return values, and other things that we put on the stack will be placed in what is sometimes called thread-local storage, i.e., the stack of the relevant thread.

You might also notice how this ruins our beautiful address space layout. Before, the stack and heap could grow independently and trouble only arose when you ran out of room in the address space. Here, we no longer have such a nice situation. Fortunately, this is usually OK, as stacks do not generally have to be very large (the exception being in programs that make heavy use of recursion).

## 26.1 An Example: Thread Creation

Let’s say we wanted to run a program that created two threads, each of which was doing some independent work, in this case printing “A” or “B”. The code is shown in Figure 26.2.

The main program creates two threads, each of which will run the function mythread(), though with different arguments (the string A or B). Once a thread is created, it may start running right away (depending on the whims of the scheduler); alternately, it may be put in a “ready” but not “running” state and thus not run yet. After creating the two threads (T1 and T2), the main thread calls pthread join(), which waits for a particular thread to complete.

```c
 1   #include <stdio.h>
 2   #include <assert.h>
 3   #include <pthread.h>
 5   void *mythread(void *arg) {
 6       printf("%s\n", (char *) arg);
 7       return NULL;
 8   }
10   int
11   main(int argc, char *argv[]) {
12       pthread_t p1, p2;
13       int rc;
14       printf("main: begin\n");
15       rc = pthread_create(&p1, NULL, mythread, "A"); assert(rc == 0);
16       rc = pthread_create(&p2, NULL, mythread, "B"); assert(rc == 0);
17       // join waits for the threads to finish
18       rc = pthread_join(p1, NULL); assert(rc == 0);
19       rc = pthread_join(p2, NULL); assert(rc == 0);
20       printf("main: end\n");
21       return 0;
22   }
```
*Figure 26.2: Simple Thread Creation Code (t0.c)*

Let us examine the possible execution ordering of this little program.

In the execution diagram (Figure 26.3, page 4), time increases in the down-

wards direction, and each column shows when a different thread (the

main one, or Thread 1, or Thread 2) is running.

Note, however, that this ordering is not the only possible ordering. In

fact, given a sequence of instructions, there are quite a few, depending on

which thread the scheduler decides to run at a given point. For example,

once a thread is created, it may run immediately, which would lead to the

execution shown in Figure 26.4 (page 4).

We also could even see “B” printed before “A”, if, say, the scheduler

```c
     decided to run Thread 2 first even though Thread 1 was created earlier;
```

there is no reason to assume that a thread that is created first will run first.

Figure 26.5 (page 4) shows this final execution ordering, with Thread 2

getting to strut its stuff before Thread 1.

As you might be able to see, one way to think about thread creation

is that it is a bit like making a function call; however, instead of first ex-

ecuting the function and then returning to the caller, the system instead

creates a new thread of execution for the routine that is being called, and

it runs independently of the caller, perhaps before returning from the cre-

ate, but perhaps much later.

As you also might be able to tell from this example, threads make life

complicated: it is already hard to tell what will run when! Computers are

hard enough to understand without concurrency. Unfortunately, with

concurrency, it gets worse. Much worse.

```text
  main                              Thread 1     Thread2
  starts running
  prints “main: begin”
  creates Thread 1
  creates Thread 2
  waits for T1
                                    runs
                                    prints “A”
                                    returns
  waits for T2
                                                 runs
                                                 prints “B”
                                                 returns
  prints “main: end”
```
*Figure 26.3: Thread Trace (1)*

```text
 main                               Thread 1     Thread2
 starts running
 prints “main: begin”
 creates Thread 1
                                    runs
                                    prints “A”
                                    returns
 creates Thread 2
                                                 runs
                                                 prints “B”
                                                 returns
 waits for T1
  returns immediately; T1 is done
 waits for T2
  returns immediately; T2 is done
 prints “main: end”
```
*Figure 26.4: Thread Trace (2)*

```text
main                                Thread 1      Thread2
starts running
prints “main: begin”
creates Thread 1
creates Thread 2
                                                  runs
                                                  prints “B”
                                                  returns
waits for T1
                                    runs
                                    prints “A”
                                    returns
waits for T2
 returns immediately; T2 is done
prints “main: end”
```
*Figure 26.5: Thread Trace (3)*

```c
 1   #include <stdio.h>
 2   #include <pthread.h>
 3   #include "mythreads.h"
 5   static volatile int counter = 0;
 7   //
 8   // mythread()
 9   //
10   // Simply adds 1 to counter repeatedly, in a loop
11   // No, this is not how you would add 10,000,000 to
12   // a counter, but it shows the problem nicely.
13   //
14   void *
15   mythread(void *arg)
16   {
17       printf("%s: begin\n", (char *) arg);
18       int i;
19       for (i = 0; i < 1e7; i++) {
20           counter = counter + 1;
21       }
22       printf("%s: done\n", (char *) arg);
23       return NULL;
24   }
26   //
27   // main()
28   //
29   // Just launches two threads (pthread_create)
30   // and then waits for them (pthread_join)
31   //
32   int
33   main(int argc, char *argv[])
34   {
35       pthread_t p1, p2;
36       printf("main: begin (counter = %d)\n", counter);
37       Pthread_create(&p1, NULL, mythread, "A");
38       Pthread_create(&p2, NULL, mythread, "B");
40       // join waits for the threads to finish
41       Pthread_join(p1, NULL);
42       Pthread_join(p2, NULL);
43       printf("main: done with both (counter = %d)\n", counter);
44       return 0;
45   }
```
*Figure 26.6: Sharing Data: Uh Oh (t1.c)*

## 26.2 Why It Gets Worse: Shared Data

The simple thread example we showed above was useful in showing

how threads are created and how they can run in different orders depend-

ing on how the scheduler decides to run them. What it doesn’t show you,

though, is how threads interact when they access shared data.

Let us imagine a simple example where two threads wish to update a global shared variable. The code we’ll study is in Figure 26.6.

Here are a few notes about the code. First, as Stevens suggests [SR05],

```c
we wrap the thread creation and join routines to simply exit on failure;
```

for a program as simple as this one, we want to at least notice an error occurred (if it did), but not do anything very smart about it (e.g., just exit). Thus, Pthread create() simply calls pthread create() and makes sure the return code is 0; if it isn’t, Pthread create() just prints a message and exits.

Second, instead of using two separate function bodies for the worker threads, we just use a single piece of code, and pass the thread an argument (in this case, a string) so we can have each thread print a different letter before its messages.

Finally, and most importantly, we can now look at what each worker is trying to do: add a number to the shared variable counter, and do so 10 million times (1e7) in a loop. Thus, the desired final result is: 20,000,000.

We now compile and run the program, to see how it behaves. Sometimes, everything works how we might expect:

```text
prompt> gcc -o main main.c -Wall -pthread
prompt> ./main
main: begin (counter = 0)
A: begin
B: begin
A: done
B: done
main: done with both (counter = 20000000)
```

Unfortunately, when we run this code, even on a single processor, we don’t necessarily get the desired result. Sometimes, we get:

```text
prompt> ./main
main: begin (counter = 0)
A: begin
B: begin
A: done
B: done
main: done with both (counter = 19345221)
```

Let’s try it one more time, just to see if we’ve gone crazy. After all, aren’t computers supposed to produce deterministic results, as you have been taught?! Perhaps your professors have been lying to you? (gasp)

```text
prompt> ./main
main: begin (counter = 0)
A: begin
B: begin
A: done
B: done
main: done with both (counter = 19221041)
```

Not only is each run wrong, but also yields a different result! A big question remains: why does this happen?

> **TIP: KNOW AND USE YOUR TOOLS**
>
> You should always learn new tools that help you write, debug, and understand computer systems. Here, we use a neat tool called a disassembler. When you run a disassembler on an executable, it shows you what assembly instructions make up the program. For example, if we wish to understand the low-level code to update a counter (as in our example), we run objdump (Linux) to see the assembly code: prompt> objdump -d main

Doing so produces a long listing of all the instructions in the program, neatly labeled (particularly if you compiled with the -g flag), which includes symbol information in the program. The objdump program is just one of many tools you should learn how to use; a debugger like gdb, memory profilers like valgrind or purify, and of course the compiler itself are others that you should spend time to learn more about; the better you are at using your tools, the better systems you’ll be able to build.

## 26.3 The Heart Of The Problem: Uncontrolled Scheduling

To understand why this happens, we must understand the code sequence that the compiler generates for the update to counter. In this case, we wish to simply add a number (1) to counter. Thus, the code

```c
sequence for doing so might look something like this (in x86);
 mov 0x8049a1c, %eax
 add $0x1, %eax
 mov %eax, 0x8049a1c
```

This example assumes that the variable counter is located at address 0x8049a1c. In this three-instruction sequence, the x86 mov instruction is used first to get the memory value at the address and put it into register eax. Then, the add is performed, adding 1 (0x1) to the contents of the eax register, and finally, the contents of eax are stored back into memory at the same address.

Let us imagine one of our two threads (Thread 1) enters this region of code, and is thus about to increment counter by one. It loads the value of counter (let’s say it’s 50 to begin with) into its register eax. Thus, eax=50 for Thread 1. Then it adds one to the register; thus eax=51. Now, something unfortunate happens: a timer interrupt goes off; thus, the OS saves the state of the currently running thread (its PC, its registers including eax, etc.) to the thread’s TCB.

Now something worse happens: Thread 2 is chosen to run, and it enters this same piece of code. It also executes the first instruction, getting the value of counter and putting it into its eax (remember: each thread when running has its own private registers; the registers are virtualized by the context-switch code that saves and restores them). The value of

(after instruction)

```text
   OS                    Thread 1                Thread 2            PC %eax counter
                         before critical section                     100       0     50
                         mov 0x8049a1c, %eax                         105      50     50
                         add $0x1, %eax                              108      51     50
   interrupt
    save T1’s state
    restore T2’s state                                               100     0     50
                                               mov 0x8049a1c, %eax   105    50     50
                                               add $0x1, %eax        108    51     50
                                               mov %eax, 0x8049a1c   113    51     51
   interrupt
    save T2’s state
    restore T1’s state                                               108    51     51
                         mov %eax, 0x8049a1c                         113    51     51
```
*Figure 26.7: The Problem: Up Close and Personal*

counter is still 50 at this point, and thus Thread 2 has eax=50. Let’s then assume that Thread 2 executes the next two instructions, incrementing eax by 1 (thus eax=51), and then saving the contents of eax into counter (address 0x8049a1c). Thus, the global variable counter now has the value 51.

Finally, another context switch occurs, and Thread 1 resumes running. Recall that it had just executed the mov and add, and is now about to perform the final mov instruction. Recall also that eax=51. Thus, the final mov instruction executes, and saves the value to memory; the counter is set to 51 again.

Put simply, what has happened is this: the code to increment counter has been run twice, but counter, which started at 50, is now only equal to 51. A “correct” version of this program should have resulted in the variable counter equal to 52.

Let’s look at a detailed execution trace to understand the problem better. Assume, for this example, that the above code is loaded at address 100 in memory, like the following sequence (note for those of you used to nice, RISC-like instruction sets: x86 has variable-length instructions; this mov instruction takes up 5 bytes of memory, and the add only 3): 100 mov        0x8049a1c, %eax 105 add        $0x1, %eax 108 mov        %eax, 0x8049a1c

With these assumptions, what happens is shown in Figure 26.7. Assume the counter starts at value 50, and trace through this example to make sure you understand what is going on.

What we have demonstrated here is called a race condition: the results depend on the timing execution of the code. With some bad luck (i.e., context switches that occur at untimely points in the execution), we get the wrong result. In fact, we may get a different result each time; thus, instead of a nice deterministic computation (which we are used to from computers), we call this result indeterminate, where it is not known what the output will be and it is indeed likely to be different across runs.

Because multiple threads executing this code can result in a race condition, we call this code a critical section. A critical section is a piece of code that accesses a shared variable (or more generally, a shared resource) and must not be concurrently executed by more than one thread.

What we really want for this code is what we call mutual exclusion. This property guarantees that if one thread is executing within the critical section, the others will be prevented from doing so.

Virtually all of these terms, by the way, were coined by Edsger Dijkstra, who was a pioneer in the field and indeed won the Turing Award because of this and other work; see his 1968 paper on “Cooperating Sequential Processes” [D68] for an amazingly clear description of the problem. We’ll be hearing more about Dijkstra in this section of the book.

## 26.4 The Wish For Atomicity

One way to solve this problem would be to have more powerful instructions that, in a single step, did exactly whatever we needed done and thus removed the possibility of an untimely interrupt. For example, what if we had a super instruction that looked like this? memory-add 0x8049a1c, $0x1

Assume this instruction adds a value to a memory location, and the hardware guarantees that it executes atomically; when the instruction executed, it would perform the update as desired. It could not be interrupted mid-instruction, because that is precisely the guarantee we receive from the hardware: when an interrupt occurs, either the instruction has not run at all, or it has run to completion; there is no in-between state. Hardware can be a beautiful thing, no?

Atomically, in this context, means “as a unit”, which sometimes we take as “all or none.” What we’d like is to execute the three instruction sequence atomically: mov 0x8049a1c, %eax add $0x1, %eax mov %eax, 0x8049a1c

As we said, if we had a single instruction to do this, we could just issue that instruction and be done. But in the general case, we won’t have such an instruction. Imagine we were building a concurrent B-tree, and wished to update it; would we really want the hardware to support an “atomic update of B-tree” instruction? Probably not, at least in a sane instruction set.

Thus, what we will instead do is ask the hardware for a few useful instructions upon which we can build a general set of what we call synchronization primitives. By using these hardware synchronization primitives, in combination with some help from the operating system, we will be able to build multi-threaded code that accesses critical sections in a

> **ASIDE: KEY CONCURRENCY TERMS CRITICAL SECTION, RACE CONDITION, INDETERMINATE, MUTUAL EXCLUSION**
>
> These four terms are so central to concurrent code that we thought it worth while to call them out explicitly. See some of Dijkstra’s early work [D65,D68] for more details. • A critical section is a piece of code that accesses a shared resource, usually a variable or data structure. • A race condition arises if multiple threads of execution enter the critical section at roughly the same time; both attempt to update the shared data structure, leading to a surprising (and perhaps undesirable) outcome. • An indeterminate program consists of one or more race conditions; the output of the program varies from run to run, depending on which threads ran when. The outcome is thus not deterministic, something we usually expect from computer systems. • To avoid these problems, threads should use some kind of mutual exclusion primitives; doing so guarantees that only a single thread ever enters a critical section, thus avoiding races, and resulting in deterministic program outputs.

synchronized and controlled manner, and thus reliably produces the correct result despite the challenging nature of concurrent execution. Pretty awesome, right?

This is the problem we will study in this section of the book. It is a wonderful and hard problem, and should make your mind hurt (a bit). If it doesn’t, then you don’t understand! Keep working until your head hurts; you then know you’re headed in the right direction. At that point, take a break; we don’t want your head hurting too much.

> **THE CRUX: HOW TO PROVIDE SUPPORT FOR SYNCHRONIZATION**
>
> What support do we need from the hardware in order to build useful synchronization primitives? What support do we need from the OS? How can we build these primitives correctly and efficiently? How can programs use them to get the desired results?

## 26.5 One More Problem: Waiting For Another

This chapter has set up the problem of concurrency as if only one type of interaction occurs between threads, that of accessing shared variables and the need to support atomicity for critical sections. As it turns out, there is another common interaction that arises, where one thread must wait for another to complete some action before it continues. This interaction arises, for example, when a process performs a disk I/O and is put to sleep; when the I/O completes, the process needs to be roused from its slumber so it can continue.

Thus, in the coming chapters, we’ll be not only studying how to build support for synchronization primitives to support atomicity but also for mechanisms to support this type of sleeping/waking interaction that is common in multi-threaded programs. If this doesn’t make sense right now, that is OK! It will soon enough, when you read the chapter on condition variables. If it doesn’t by then, well, then it is less OK, and you should read that chapter again (and again) until it does make sense.

## 26.6 Summary: Why in OS Class?

Before wrapping up, one question that you might have is: why are we studying this in OS class? “History” is the one-word answer; the OS was the first concurrent program, and many techniques were created for use within the OS. Later, with multi-threaded processes, application programmers also had to consider such things.

For example, imagine the case where there are two processes running. Assume they both call write() to write to the file, and both wish to append the data to the file (i.e., add the data to the end of the file, thus increasing its length). To do so, both must allocate a new block, record in the inode of the file where this block lives, and change the size of the file to reflect the new larger size (among other things; we’ll learn more about files in the third part of the book). Because an interrupt may occur at any time, the code that updates these shared structures (e.g., a bitmap for allocation, or the file’s inode) are critical sections; thus, OS designers, from the very beginning of the introduction of the interrupt, had to worry about how the OS updates internal structures. An untimely interrupt causes all of the problems described above. Not surprisingly, page tables, process lists, file system structures, and virtually every kernel data structure has to be carefully accessed, with the proper synchronization primitives, to work correctly.

> **TIP: USE ATOMIC OPERATIONS**
>
> Atomic operations are one of the most powerful underlying techniques in building computer systems, from the computer architecture, to concurrent code (what we are studying here), to file systems (which we’ll study soon enough), database management systems, and even distributed systems [L+93]. The idea behind making a series of actions atomic is simply expressed with the phrase “all or nothing”; it should either appear as if all of the actions you wish to group together occurred, or that none of them occurred, with no in-between state visible. Sometimes, the grouping of many actions into a single atomic action is called a transaction, an idea developed in great detail in the world of databases and transaction processing [GR92]. In our theme of exploring concurrency, we’ll be using synchronization primitives to turn short sequences of instructions into atomic blocks of execution, but the idea of atomicity is much bigger than that, as we will see. For example, file systems use techniques such as journaling or copyon-write in order to atomically transition their on-disk state, critical for operating correctly in the face of system failures. If that doesn’t make sense, don’t worry — it will, in some future chapter.

.

## References

[D65] “Solution of a problem in concurrent programming control” E. W. Dijkstra Communications of the ACM, 8(9):569, September 1965 Pointed to as the first paper of Dijkstra’s where he outlines the mutual exclusion problem and a solution. The solution, however, is not widely used; advanced hardware and OS support is needed, as we will see in the coming chapters.

[D68] “Cooperating sequential processes” Edsger W. Dijkstra, 1968 Available: http://www.cs.utexas.edu/users/EWD/ewd01xx/EWD123.PDF Dijkstra has an amazing number of his old papers, notes, and thoughts recorded (for posterity) on this website at the last place he worked, the University of Texas. Much of his foundational work, however, was done years earlier while he was at the Technische Hochshule of Eindhoven (THE), including this famous paper on “cooperating sequential processes”, which basically outlines all of the thinking that has to go into writing multi-threaded programs. Dijkstra discovered much of this while working on an operating system named after his school: the “THE” operating system (said “T”, “H”, “E”, and not like the word “the”).

[GR92] “Transaction Processing: Concepts and Techniques” Jim Gray and Andreas Reuter Morgan Kaufmann, September 1992 This book is the bible of transaction processing, written by one of the legends of the field, Jim Gray. It is, for this reason, also considered Jim Gray’s “brain dump”, in which he wrote down everything he knows about how database management systems work. Sadly, Gray passed away tragically a few years back, and many of us lost a friend and great mentor, including the co-authors of said book, who were lucky enough to interact with Gray during their graduate school years.

[L+93] “Atomic Transactions” Nancy Lynch, Michael Merritt, William Weihl, Alan Fekete Morgan Kaufmann, August 1993 A nice text on some of the theory and practice of atomic transactions for distributed systems. Perhaps a bit formal for some, but lots of good material is found herein.

[SR05] “Advanced Programming in the UNIX Environment” W. Richard Stevens and Stephen A. Rago Addison-Wesley, 2005 As we’ve said many times, buy this book, and read it, in little chunks, preferably before going to bed. This way, you will actually fall asleep more quickly; more importantly, you learn a little more about how to become a serious UNIX programmer.

## Homework

This program, x86.py, allows you to see how different thread interleavings either cause or avoid race conditions. See the README for details on how the program works and its basic inputs, then answer the questions below.

Questions 1. To start, let’s examine a simple program, “loop.s”. First, just look at the program, and see if you can understand it: cat loop.s. Then, run it with these arguments:

./x86.py -p loop.s -t 1 -i 100 -R dx

This specifies a single thread, an interrupt every 100 instructions, and tracing of register %dx. Can you figure out what the value of %dx will be during the run? Once you have, run the same above and use the -c flag to check your answers; note the answers, on the left, show the value of the register (or memory value) after the instruction on the right has run. 2. Now run the same code but with these flags:

./x86.py -p loop.s -t 2 -i 100 -a dx=3,dx=3 -R dx

This specifies two threads, and initializes each %dx register to 3. What values will %dx see? Run with the -c flag to see the answers. Does the presence of multiple threads affect anything about your calculations? Is there a race condition in this code? 3. Now run the following:

./x86.py -p loop.s -t 2 -i 3 -r -a dx=3,dx=3 -R dx

This makes the interrupt interval quite small and random; use different seeds with -s to see different interleavings. Does the frequency of interruption change anything about this program? 4. Next we’ll examine a different program (looping-race-nolock.s). This program accesses a shared variable located at memory address 2000; we’ll call this variable x for simplicity. Run it with a single thread and make sure you understand what it does, like this:

./x86.py -p looping-race-nolock.s -t 1 -M 2000

What value is found in x (i.e., at memory address 2000) throughout the run? Use -c to check your answer.

5. Now run with multiple iterations and threads: ./x86.py -p looping-race-nolock.s -t 2 -a bx=3 -M 2000

Do you understand why the code in each thread loops three times? What will the final value of x be? 6. Now run with random interrupt intervals: ./x86.py -p looping-race-nolock.s -t 2 -M 2000 -i 4 -r -s 0

Then change the random seed, setting -s 1, then -s 2, etc. Can you tell, just by looking at the thread interleaving, what the final value of x will be? Does the exact location of the interrupt matter? Where can it safely occur? Where does an interrupt cause trouble? In other words, where is the critical section exactly? 7. Now use a fixed interrupt interval to explore the program further. Run: ./x86.py -p looping-race-nolock.s -a bx=1 -t 2 -M 2000 -i 1

See if you can guess what the final value of the shared variable x will be. What about when you change -i 2, -i 3, etc.? For which interrupt intervals does the program give the “correct” final answer? 8. Now run the same code for more loops (e.g., set -a bx=100). What interrupt intervals, set with the -i flag, lead to a “correct” outcome? Which intervals lead to surprising results? 9. We’ll examine one last program in this homework (wait-for-me.s). Run the code like this: ./x86.py -p wait-for-me.s -a ax=1,ax=0 -R ax -M 2000

This sets the %ax register to 1 for thread 0, and 0 for thread 1, and watches the value of %ax and memory location 2000 throughout the run. How should the code behave? How is the value at location 2000 being used by the threads? What will its final value be? 10. Now switch the inputs: ./x86.py -p wait-for-me.s -a ax=0,ax=1 -R ax -M 2000

How do the threads behave? What is thread 0 doing? How would changing the interrupt interval (e.g., -i 1000, or perhaps to use random intervals) change the trace outcome? Is the program efficiently using the CPU?
