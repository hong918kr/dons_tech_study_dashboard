# F. Laboratory: Tutorial

> 원문: *Operating Systems: Three Easy Pieces* (v0.91) — Part **Appendix**, PDF p.637–648 · [PDF 원본에서 보기 (그림 포함)](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=637)

> ⚠️ PDF 텍스트 자동 추출본. 그림·표는 깨질 수 있으니 그림이 나오면 PDF 링크로 확인.

This is a very brief document to familiarize you with the basics of the C programming environment on UNIX systems. It is not comprehensive or particularly detailed, but should just give you enough to get you going.

A couple of general points of advice about programming: if you want to become an expert programmer, you need to master more than just the syntax of a language. Specifically, you should know your tools, know your libraries, and know your documentation. The tools that are relevant to C compilation are gcc, gdb, and maybe ld. There are tons of library routines that are also available to you, but fortunately a lot of functionality is included in libc, which is linked with all C programs by default — all you need to do is include the right header files. Finally, knowing how to find the library routines you need (e.g., learning to find and read man pages) is a skill worth acquiring. We’ll talk about each of these in more detail later on.

Like (almost) everything worth doing in life, becoming an expert in these domains takes time. Spending the time up-front to learn more about the tools and environment is definitely well worth the effort.

## F.1 A Simple C Program

We’ll start with a simple C program, perhaps saved in the file “hw.c”. Unlike Java, there is not necessarily a connection between the file name and the contents of the file; thus, use your common sense in naming files in a manner that is appropriate.

The first line specifies a file to include, in this case stdio.h, which “prototypes” many of the commonly used input/output routines; the one we are interested in is printf(). When you use the #include directive, you are telling the C preprocessor (cpp) to find a particular file (e.g., stdio.h) and to insert it directly into your code at the spot of the

```c
#include. By default, cpp will look in the directory /usr/include/
to try to find the file.
```

The next part specifies the signature of the main() routine, namely that it returns an integer (int), and will be called with two arguments,

```c
/* header files go up here */
/* note that C comments are enclosed within a slash and a star, and
   may wrap over lines */
// if you use gcc, two slashes will work too (and may be preferred)
#include <stdio.h>
/* main returns an integer */
int main(int argc, char *argv[]) {
    /* printf is our output function;
       by default, writes to standard out */
    /* printf returns an integer, but we ignore that */
    printf("hello, world\n");
    /* return 0 to indicate all went well */
    return(0);
}
```

an integer argc, which is a count of the number of arguments on the command line, and an array of pointers to characters (argv), each of which contain a word from the command line, and the last of which is null. There will be more on pointers and arrays below.

The program then simply prints the string “hello, world” and advances the output stream to the next line, courtesy of the backslash followed by an “n” at the end of the call to printf(). Afterwards, the program completes by returning a value, which is passed back to the shell that executed the program. A script or the user at the terminal could check this value (in csh and tcsh shells, it is stored in the status variable), to see whether the program exited cleanly or with an error.

## F.2 Compilation and Execution

We’ll now learn how to compile the program. Note that we will use gcc as our example, though on some platforms you may be able to use a different (native) compiler, cc.

At the shell prompt, you just type:

```text
prompt> gcc hw.c
```

gcc is not really the compiler, but rather the program called a “compiler driver”; thus it coordinates the many steps of the compilation. Usually there are four to five steps. First, gcc will execute cpp, the C preprocessor, to process certain directives (such as #define and #include. The program cpp is just a source-to-source translator, so its end-product is still just source code (i.e., a C file). Then the real compilation will begin, usually a command called cc1. This will transform source-level C code into low-level assembly code, specific to the host machine. The assembler as will then be executed, generating object code (bits and things that machines can really understand), and finally the link-editor (or linker) ld will put it all together into a final executable program. Fortunately(!), for most purposes, you can blithely be unaware of how gcc works, and just use it with the proper flags.

The result of your compilation above is an executable, named (by default) a.out. To then run the program, we simply type:

```text
prompt> ./a.out
```

When we run this program, the OS will set argc and argv properly so that the program can process the command-line arguments as need be. Specifically, argc will be equal to 1, argv[0] will be the string “./a.out”, and argv[1] will be null, indicating the end of the array.

## F.3 Useful Flags

Before moving on to the C language, we’ll first point out some useful compilation flags for gcc.

```text
prompt> gcc -o hw hw.c           # -o: to specify the executable name
prompt> gcc -Wall hw.c           # -Wall: gives much better warnings
prompt> gcc -g hw.c              # -g: to enable debugging with gdb
prompt> gcc -O hw.c              # -O: to turn on optimization
```

Of course, you may combine these flags as you see fit (e.g., gcc -o hw -g -Wall hw.c). Of these flags, you should always use -Wall, which gives you lots of extra warnings about possible mistakes. Don’t ignore the warnings! Instead, fix them and thus make them blissfully disappear.

## F.4 Linking with Libraries

Sometimes, you may want to use a library routine in your program. Because so many routines are available in the C library (which is automatically linked with every program), all you usually have to do is find the right #include file. The best way to do that is via the manual pages, usually just called the man pages.

For example, let’s say you want to use the fork() system call1 . By typing man fork at the shell prompt, you will get back a text description of how fork() works. At the very top will be a short code snippet, and that will tell you which files you need to #include in your program in order to get it to compile. In the case of fork(), you need to #include both sys/types.h and unistd.h, which would be accomplished as follows:

```c
#include <sys/types.h>
#include <unistd.h>
```

Note that fork() is a system call, and not just a library routine. However, the C library provides C wrappers for all the system calls, each of which simply trap into the operating system.

However, some library routines do not reside in the C library, and therefore you will have to do a little more work. For example, the math library has many useful routines, such as sines, cosines, tangents, and the like. If you want to include the routine tan() in our code, you should again first check the man page. At the top of the Linux man page for tan, you will see the following two lines:

```c
       #include <math.h>
       ...
       Link with -lm.
```

The first line you already should understand — you need to #include the math library, which is found in the standard location in the file system (i.e., /usr/include/math.h). However, what the next line is telling you is how to “link” your program with the math library. A number of useful libraries exist and can be linked with; many of those reside in /usr/lib; it is indeed where the math library is found.

There are two types of libraries: statically-linked libraries (which end in .a), and dynamically-linked ones (which end in .so). Statically-linked libraries are combined directly into your executable; that is, the low-level code for the library is inserted into your executable by the linker, and results in a much larger binary object. Dynamic linking improves on this

```c
by just including the reference to a library in your program executable;
```

when the program is run, the operating system loader dynamically links in the library. This method is preferred over the static approach because it saves disk space (no unnecessarily large executables are made) and allows applications to share library code and static data in memory. In the case of the math library, both static and dynamic versions are available, with the static version called /usr/lib/libm.a and the dynamic one /usr/lib/libm.so.

In any case, to link with the math library, you need to specify the library to the link-editor; this can be achieved by invoking gcc with the right flags.

```text
prompt> gcc -o hw hw.c -Wall -lm
```

The -lXXX flag tells the linker to look for libXXX.so or libXXX.a, probably in that order. If for some reason you insist on the static library over the dynamic one, there is another flag you can use — see you if you can find out what it is. People sometimes prefer the static version of a library because of the slight performance cost associated with using dynamic libraries.

One final note: if you want the compiler to search for headers in a different path than the usual places, or want it to link with libraries that you specify, you can use the compiler flag -I/foo/bar to look for headers in the directory /foo/bar, and the -L/foo/bar flag to look for libraries in the /foo/bar directory. One common directory to specify in this manner is “.” (called “dot”), which is UNIX shorthand for the current directory.

Note that the -I flag should go on a compile line, and the -L flag on the link line.

## F.5 Separate Compilation

Once a program starts to get large enough, you may want to split it into separate files, compiling each separately, and then link them together. For example, say you have two files, hw.c and helper.c, and you wish to compile them individually, and then link them together. # we are using -Wall for warnings, -O for optimization

```text
prompt> gcc -Wall -O -c hw.c
prompt> gcc -Wall -O -c helper.c
prompt> gcc -o hw hw.o helper.o -lm
```

The -c flag tells the compiler just to produce an object file — in this case, files called hw.o and helper.o. These files are not executables, but just machine-level representations of the code within each source file. To combine the object files into an executable, you have to “link” them together; this is accomplished with the third line gcc -o hw hw.o helper.o). In this case, gcc sees that the input files specified are not source files (.c), but instead are object files (.o), and therefore skips right to the last step and invoked the link-editor ld to link them together into a single executable. Because of its function, this line is often called the “link line”, and would be where you specify link-specific commands such as -lm. Analogously, flags such as -Wall and -O are only needed in the compile phase, and therefore need not be included on the link line but rather only on compile lines.

Of course, you could just specify all the C source files on a single line to gcc (gcc -Wall -O -o hw hw.c helper.c), but this requires the system to recompile every source-code file, which can be a time-consuming process. By compiling each individually, you can save time by only recompiling those files that have changed during your editing, and thus increase your productivity. This process is best managed by another program, make, which we now describe.

## F.6 Makefiles

The program make lets you automate much of your build process, and is thus a crucially important tool for any serious program (and programmer). Let’s take a look at a simple example, saved in a file called Makefile.

To build your program, now all you have to do is type:

```text
prompt> make
```

This will (by default) look for Makefile or makefile, and use that as its input (you can specify a different makefile with a flag; read the

hw: hw.o helper.o

gcc -o hw hw.o helper.o -lm

hw.o: hw.c

gcc -O -Wall -c hw.c

helper.o: helper.c

gcc -O -Wall -c helper.c

clean:

rm -f hw.o helper.o hw man pages to find out which). The gnu version of make, gmake, is more fully featured than traditional make, so we will focus upon it for the rest of this discussion (though we will use the two terms interchangeably). Most of these notes are based on the gmake info page; to see how to find those pages, see the Documentation section below. Also note: on Linux systems, gmake and make are one and the same.

Makefiles are based on rules, which are used to decide what needs to happen. The general form of a rule: target: prerequisite1 prerequisite2 ...

command1

command2

...

```c
   A target is usually the name of a file that is generated by a program;
```

examples of targets are executable or object files. A target can also be the name of an action to carry out, such as “clean” in our example.

A prerequisite is a file that is used as input to create the target. A target often depends on several files. For example, to build the executable hw, we need two object files to be built first: hw.o and helper.o.

Finally, a command is an action that make carries out. A rule may have more than one command, each on its own line. Important: You have to put a single tab character at the beginning of every command line! If you just put spaces, make will print out some obscure error message and exit.

Usually a command is in a rule with prerequisites and serves to create a target file if any of the prerequisites change. However, the rule that specifies commands for the target need not have prerequisites. For example, the rule containing the delete command associated with the target “clean” does not have prerequisites.

Going back to our example, when make is executed, it roughly works like this: First, it comes to the target hw, and it realizes that to build it, it must have two prerequisites, hw.o and helper.o. Thus, hw depends on those two object files. Make then will examine each of those targets. In examining hw.o, it will see that it depends on hw.c. Here is the key: if hw.c has been modified more recently than hw.o has been created, make will know that hw.o is out of date and should be generated anew; in that case, it will execute the command line, gcc -O -Wall -c hw.c, which generates hw.o. Thus, if you are compiling a large program, make will know which object files need to be re-generated based on their depen-

dencies, and will only do the necessary amount of work to recreate the executable. Also note that hw.o will be created in the case that it does not exist at all.

Continuing along, helper.o may also be regenerated or created, based on the same criteria as defined above. When both of the object files have been created, make is now ready to execute the command to create the final executable, and goes back and does so: gcc -o hw hw.o helper.o -lm.

Up until now, we’ve been ignoring the clean target in the makefile. To use it, you have to ask for it explicitly. Type

```text
prompt> make clean
```

This will execute the command on the command line. Because there are no prerequisites for the clean target, typing make clean will always result in the command(s) being executed. In this case, the clean target is used to remove the object files and executable, quite handy if you wish to rebuild the entire program from scratch.

Now you might be thinking, “well, this seems OK, but these makefiles sure are cumbersome!” And you’d be right — if they always had to be written like this. Fortunately, there are a lot of shortcuts that make make even easier to use. For example, this makefile has the same functionality but is a little nicer to use: # specify all source files here SRCS = hw.c helper.c # specify target here (name of executable) TARG = hw # specify compiler, compile flags, and needed libs CC   = gcc OPTS = -Wall -O LIBS = -lm

# this translates .c files in src list to .o’s OBJS = $(SRCS:.c=.o)

# all is not really needed, but is used to generate the target all: $(TARG)

# this generates the target executable $(TARG): $(OBJS)

$(CC) -o $(TARG) $(OBJS) $(LIBS)

# this is a generic rule for .o files %.o: %.c

$(CC) $(OPTS) -c $< -o $@

# and finally, a clean line clean:

rm -f $(OBJS) $(TARG)

Though we won’t go into the details of make syntax, as you can see, this makefile can make your life somewhat easier. For example, it allows

you to easily add new source files into your build, simply by adding them to the SRCS variable at the top of the makefile. You can also easily change the name of the executable by changing the TARG line, and the compiler, flags, and library specifications are all easily modified.

One final word about make: figuring out a target’s prerequisites is not always trivial, especially in large and complex programs. Not surprisingly, there is another tool that helps with this, called makedepend. Read about it on your own and see if you can incorporate it into a makefile.

## F.7 Debugging

Finally, after you have created a good build environment, and a correctly compiled program, you may find that your program is buggy. One way to fix the problem(s) is to think really hard — this method is sometimes successful, but often not. The problem is a lack of information; you just don’t know exactly what is going on within the program, and therefore cannot figure out why it is not behaving as expected. Fortunately, there is some help: gdb, the GNU debugger.

Let’s take the following buggy code, saved in the file buggy.c, and compiled into the executable buggy.

```c
#include <stdio.h>
struct Data {
    int x;
};
int
main(int argc, char *argv[])
{
    struct Data *p = NULL;
    printf("%d\n", p->x);
}
```

In this example, the main program dereferences the variable p when it is NULL, which will lead to a segmentation fault. Of course, this problem should be easy to fix by inspection, but in a more complex program, finding such a problem is not always easy.

To prepare yourself for a debugging session, recompile your program and make sure to pass the -g flag to each compile line. This includes extra debugging information in your executable that will be useful during your debugging session. Also, don’t turn on optimization (-O); though this may work, it may also lead to confusion during debugging.

After re-compiling with -g, you are ready to use the debugger. Fire up gdb at the command prompt as follows:

```text
prompt> gdb buggy
```

This puts you inside an interactive session with the debugger. Note that you can also use the debugger to examine “core” files that were pro-

duced during bad runs, or to attach to an already-running program; read the documentation to learn more about this.

Once inside, you may see something like this:

```text
prompt> gdb buggy
GNU gdb ...
Copyright 2008 Free Software Foundation, Inc.
(gdb)
```

The first thing you might want to do is to go ahead and run the program. To do this, simply type run at gdb command prompt. In this case, this is what you might see: (gdb) run Starting program: buggy

Program received signal SIGSEGV, Segmentation fault. 0x8048433 in main (argc=1, argv=0xbffff844) at buggy.cc:19

```c
19          printf("%d\n", p->x);
```

As you can see from the example, in this case, gdb immediately pinpoints where the problem occurred; a “segmentation fault” was generated at the line where we tried to dereference p. This just means that we accessed some memory that we weren’t supposed to access. At this point, the astute programmer can examine the code, and say “aha! it must be that p does not point to anything valid, and thus should not be dereferenced!”, and then go ahead and fix the problem.

However, if you didn’t know what was going on, you might want to examine some variable. gdb allows you to do this interactively during the debug session. (gdb) print p 1 = (Data *) 0x0

By using the print primitive, we can examine p, and see both that it is a pointer to a struct of type Data, and that it is currently set to NULL (or zero, or hex zero which is shown here as “0x0”).

Finally, you can also set breakpoints within your program to have the debugger stop the program at a certain routine. After doing this, it is often useful to step through the execution (one line at a time), and see what is happening. (gdb) break main Breakpoint 1 at 0x8048426: file buggy.cc, line 17. (gdb) run Starting program: /homes/hacker/buggy

Breakpoint 1, main (argc=1, argv=0xbffff844) at buggy.cc:17

```c
17          struct Data *p = NULL;
(gdb) next
19          printf("%d\n", p->x);
(gdb)
Program received signal SIGSEGV, Segmentation fault.
0x8048433 in main (argc=1, argv=0xbffff844) at buggy.cc:19
19          printf("%d\n", p->x);
```

In the example above, a breakpoint is set at the main() routine; thus, when we run the program, the debugger almost immediately stops execution at main. At that point in the example, a “next” command is issued, which executes the next source-level command. Both “next” and “step” are useful ways to advance through a program — read about them in the documentation for more details 2 .

This discussion really does not do gdb justice; it is a rich and flexible debugging tool, with many more features than can be described in the limited space here. Read more about it on your own and become an expert in your copious spare time.

## F.8 Documentation

To learn a lot more about all of these things, you have to do two things: the first is to use these tools, and the second is to read more about them on your own. One way to find out more about gcc, gmake, and gdb is to read their man pages; type man gcc, man gmake, or man gdb at your command prompt. You can also use man -k to search the man pages for keywords, though that doesn’t always work as well as it might; googling is probably a better approach here.

One tricky thing about man pages: typing man XXX may not result in the thing you want, if there is more than one thing called XXX. For example, if you are looking for the kill() system call man page, and if you just type man kill at the prompt, you will get the wrong man page, because there is a command-line program called kill. Man pages are divided into sections, and by default, man will return the man page in the lowest section that it finds, which in this case is section 1. Note that you can tell which man page you got by looking at the top of the page: if you see kill(2), you know you are in the right man page in Section 2, where system calls live. Type man man to learn more about what is stored in each of the different sections of the man pages. Also note that man -a kill can be used to cycle through all of the different man pages named “kill”.

Man pages are useful for finding out a number of things. In particular, you will often want to look up what arguments to pass to a library call, or what header files need to be included to use a library call. All of this should be available in the man page. For example, if you look up the open() system call, you will see:

SYNOPSIS

```c
     #include <sys/types.h>
     #include <sys/stat.h>
     #include <fcntl.h>
        int open(const char *path, int oflag, /* mode_t mode */...);
```

In particular, you can use the interactive “help” command while debugging with gdb

That tells you to include the headers sys/types.h, sys/stat.h, and fcntl.h in order to use the open call. It also tells you about the parameters to pass to open, namely a string called path, and integer flag oflag, and an optional argument to specify the mode of the file. If there were any libraries you needed to link with to use the call, it would tell you that here too.

Man pages require some effort to use effectively. They are often divided into a number of standard sections. The main body will describe how you can pass different parameters in order to have the function behave differently.

One particularly useful section is called the RETURN VALUES part of the man page, and it tells you what the function will return under success or failure. From the open() man page again: RETURN VALUES

Upon successful completion, the open() function opens the

file and return a non-negative integer representing the

lowest numbered unused file descriptor. Otherwise, -1 is

returned, errno is set to indicate the error, and no files

are created or modified.

Thus, by checking what open returns, you can see if the open succeeded or not. If it didn’t, open (and many standard library routines) will set a global variable called errno to a value to tell you about the error. See the ERRORS section of the man page for more details.

Another thing you might want to do is to look for the definition of a structure that is not specified in the man page itself. For example, the man page for gettimeofday() has the following synopsis: SYNOPSIS

```c
     #include <sys/time.h>
     int gettimeofday(struct timeval *restrict tp,
                      void *restrict tzp);
```

From this page, you can see that the time is put into a structure of type timeval, but the man page may not tell you what fields that struct has! (in this case, it does, but you may not always be so lucky) Thus, you may have to hunt for it. All include files are found under the directory /usr/include, and thus you can use a tool like grep to look for it. For example, you might type:

```text
prompt> grep ’struct timeval’ /usr/include/sys/*.h
```

This lets you look for the definition of the structure in all files that end with .h in /usr/include/sys. Unfortunately, this may not always work, as that include file may include others which are found elsewhere.

A better way to do this is to use a tool at your disposal, the compiler. Write a program that includes the header time.h, let’s say called main.c. Then, instead of compiling it, use the compiler to invoke the preprocessor. The preprocessor processes all the directives in your file, such as #define commands and #include commands. To do this, type

gcc -E main.c. The result of this is a C file that has all of the needed structures and prototypes in it, including the definition of the timeval struct.

Probably an even better way to find these things out: google. You should always google things you don’t know about — it’s amazing how much you can learn simply by looking it up!

Info Pages Also quite useful in the hunt for documentation are the info pages, which provide much more detailed documentation on many GNU tools. You can access the info pages by running the program info, or via emacs, the preferred editor of hackers, by executing Meta-x info. A program like gcc has hundreds of flags, and some of them are surprisingly useful to know about. gmake has many more features that will improve your build environment. Finally, gdb is quite a sophisticated debugger. Read the man and info pages, try out features that you hadn’t tried before, and become a power user of your programming tools.

## F.9 Suggested Readings

Other than the man and info pages, there are a number of useful books

```c
out there. Note that a lot of this information is available for free on-line;
```

however, sometimes having something in book form seems to make it easier to learn. Also, always look for O’Reilly books on topics you are interested in; they are almost always of high quality.

- “The C Programming Language”, by Brian Kernighan and Dennis Ritchie. This is the definitive C book to have.

- “Managing Projects with make”, by Andrew Oram and Steve Talbott. A reasonable and short book on make.

- “Debugging with GDB: The GNU Source-Level Debugger”, by Richard M. Stallman, Roland H. Pesch. A little book on using GDB.

- “Advanced Programming in the UNIX Environment”, by W. Richard Stevens and Steve Rago. Stevens wrote some excellent books, and this is a must for UNIX hackers. He also has an excellent set of books on TCP/IP and Sockets programming.

- “Expert C Programming”, by Peter Van der Linden. A lot of the useful tips about compilers, etc., above are stolen directly from here. Read this! It is a great and eye-opening book, even though a little out of date.
