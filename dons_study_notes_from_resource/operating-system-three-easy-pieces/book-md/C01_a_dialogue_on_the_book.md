# 01. A Dialogue on the Book

> 원문: *Operating Systems: Three Easy Pieces* (v0.91) — Part **Intro**, PDF p.22–23 · [PDF 원본에서 보기 (그림 포함)](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=22)

> ⚠️ PDF 텍스트 자동 추출본. 그림·표는 깨질 수 있으니 그림이 나오면 PDF 링크로 확인.

Professor: Welcome to this book! It’s called Operating Systems in Three Easy Pieces, and I am here to teach you the things you need to know about operating systems. I am called “Professor”; who are you? Student: Hi Professor! I am called “Student”, as you might have guessed. And I am here and ready to learn! Professor: Sounds good. Any questions? Student: Sure! Why is it called “Three Easy Pieces”? Professor: That’s an easy one. Well, you see, there are these great lectures on Physics by Richard Feynman... Student: Oh! The guy who wrote “Surely You’re Joking, Mr. Feynman”, right? Great book! Is this going to be hilarious like that book was? Professor: Um... well, no. That book was great, and I’m glad you’ve read it. Hopefully this book is more like his notes on Physics. Some of the basics were

```c
summed up in a book called “Six Easy Pieces”. He was talking about Physics;
```

we’re going to do Three Easy Pieces on the fine topic of Operating Systems. This is appropriate, as Operating Systems are about half as hard as Physics. Student: Well, I liked physics, so that is probably good. What are those pieces? Professor: They are the three key ideas we’re going to learn about: virtualization, concurrency, and persistence. In learning about these ideas, we’ll learn all about how an operating system works, including how it decides what program to run next on a CPU, how it handles memory overload in a virtual memory system, how virtual machine monitors work, how to manage information on disks, and even a little about how to build a distributed system that works when parts have failed. That sort of stuff. Student: I have no idea what you’re talking about, really. Professor: Good! That means you are in the right class. Student: I have another question: what’s the best way to learn this stuff? Professor: Excellent query! Well, each person needs to figure this out on their

own, of course, but here is what I would do: go to class, to hear the professor introduce the material. Then, at the end of every week, read these notes, to help the ideas sink into your head a bit better. Of course, some time later (hint: before the exam!), read the notes again to firm up your knowledge. Of course, your pro-

```c
fessor will no doubt assign some homeworks and projects, so you should do those;
```

in particular, doing projects where you write real code to solve real problems is the best way to put the ideas within these notes into action. As Confucius said... Student: Oh, I know! ’I hear and I forget. I see and I remember. I do and I understand.’ Or something like that. Professor: (surprised) How did you know what I was going to say?! Student: It seemed to follow. Also, I am a big fan of Confucius, and an even bigger fan of Xunzi, who actually is a better source for this quote1 . Professor: (stunned) Well, I think we are going to get along just fine! Just fine indeed. Student: Professor – just one more question, if I may. What are these dialogues for? I mean, isn’t this just supposed to be a book? Why not present the material directly? Professor: Ah, good question, good question! Well, I think it is sometimes useful to pull yourself outside of a narrative and think a bit; these dialogues are those times. So you and I are going to work together to make sense of all of these pretty complex ideas. Are you up for it? Student: So we have to think? Well, I’m up for that. I mean, what else do I have to do anyhow? It’s not like I have much of a life outside of this book. Professor: Me neither, sadly. So let’s get to work!

According to this website (http://www.barrypopik.com/index.php/new york city/ entry/tell me and i forget teach me and i may remember involve me and i will lear/), Confucian philosopher Xunzi said “Not having heard something is not as good as having heard it; having heard it is not as good as having seen it; having seen it is not as good as knowing it; knowing it is not as good as putting it into practice.” Later on, the wisdom got attached to Confucius for some reason. Thanks to Jiao Dong (Rutgers) for telling us!
