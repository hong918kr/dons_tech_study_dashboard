# E. A Dialogue on Labs

> 원문: *Operating Systems: Three Easy Pieces* (v0.91) — Part **Appendix**, PDF p.635–636 · [PDF 원본에서 보기 (그림 포함)](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=635)

> ⚠️ PDF 텍스트 자동 추출본. 그림·표는 깨질 수 있으니 그림이 나오면 PDF 링크로 확인.

Student: Is this our final dialogue? Professor: I hope so! You’ve been becoming quite a pain, you know! Student: Yes, I’ve enjoyed our conversations too. What’s up here?

```c
Professor: It’s about the projects you should be doing as you learn this material;
```

you know, actual programming, where you do some real work instead of this incessant talking and reading. The real way to learn! Student: Sounds important. Why didn’t you tell me earlier? Professor: Well, hopefully those using this book actually do look at this part earlier, all throughout the course. If not, they’re really missing something. Student: Seems like it. So what are the projects like? Professor: Well, there are two types of projects. The first set are what you might call systems programming projects, done on machines running Linux and in the C programming environment. This type of programming is quite useful to know, as when you go off into the real world, you very well might have to do some of this type of hacking yourself. Student: What’s the second type of project? Professor: The second type is based inside a real kernel, a cool little teaching kernel developed at MIT called xv6. It is a “port” of an old version of UNIX to Intel x86, and is quite neat! With these projects, instead of writing code that interacts with the kernel (as you do in systems programming), you actually get to re-write parts of the kernel itself! Student: Sounds fun! So what should we do in a semester? You know, there are only so many hours in the day, and as you professors seem to forget, we students take four or five courses, not just yours! Professor: Well, there is a lot of flexibility here. Some classes just do all systems programming, because it is so practical. Some classes do all xv6 hacking, because it really gets you to see how operating systems work. And some, as you may have guessed, do a mix, starting with some systems programming, and then doing xv6 at the end. It’s really up to the professor of a particular class.

Student: (sighing) Professors have all the control, it seems... Professor: Oh, hardly! But that little control they do get to exercise is one of the fun parts of the job. Deciding on assignments is important you know — and not something any professor takes lightly. Student: Well, that is good to hear. I guess we should see what these projects are all about... Professor: OK. And one more thing: if you’re interested in the systems programming part, there is also a little tutorial about the UNIX and C programming environment. Student: Sounds almost too useful to be true. Professor: Well, take a look. You know, classes are supposed to be about useful things, sometimes!
