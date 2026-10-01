# 39. Interlude: Files and Directories

> 원문: *Operating Systems: Three Easy Pieces* (v0.91) — Part **Persistence**, PDF p.459–477 · [PDF 원본에서 보기 (그림 포함)](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=459)

> ⚠️ PDF 텍스트 자동 추출본. 그림·표는 깨질 수 있으니 그림이 나오면 PDF 링크로 확인.

Thus far we have seen the development of two key operating system abstractions: the process, which is a virtualization of the CPU, and the address space, which is a virtualization of memory. In tandem, these two abstractions allow a program to run as if it is in its own private, isolated world; as if it has its own processor (or processors); as if it has its own memory. This illusion makes programming the system much easier and thus is prevalent today not only on desktops and servers but increasingly on all programmable platforms including mobile phones and the like.

In this section, we add one more critical piece to the virtualization puzzle: persistent storage. A persistent-storage device, such as a classic hard disk drive or a more modern solid-state storage device, stores information permanently (or at least, for a long time). Unlike memory, whose contents are lost when there is a power loss, a persistent-storage device keeps such data intact. Thus, the OS must take extra care with such a device: this is where users keep data that they really care about.

> **CRUX: HOW TO MANAGE AP ERSISTENT DEVICE**
>
> How should the OS manage a persistent device? What are the APIs? What are the important aspects of the implementation?

Thus, in the next few chapters, we will explore critical techniques for managing persistent data, focusing on methods to improve performance and reliability. We begin, however, with an overview of the API: the interfaces you’ll expect to see when interacting with a UNIX file system.

## 39.1 Files and Directories

Two key abstractions have developed over time in the virtualization of storage. The first is the file. A file is simply a linear array of bytes, each of which you can read or write. Each file has some kind of low-level

name, usually a number of some kind; often, the user is not aware of this name (as we will see). For historical reasons, the low-level name of a file is often referred to as its inode number. We’ll be learning a lot more about inodes in future chapters; for now, just assume that each file has an inode number associated with it.

In most systems, the OS does not know much about the structure of the file (e.g., whether it is a picture, or a text file, or C code); rather, the responsibility of the file system is simply to store such data persistently on disk and make sure that when you request the data again, you get what you put there in the first place. Doing so is not as simple as it seems!

The second abstraction is that of a directory. A directory, like a file, also has a low-level name (i.e., an inode number), but its contents are quite specific: it contains a list of (user-readable name, low-level name) pairs. For example, let’s say there is a file with the low-level name “10”, and it is referred to by the user-readable name of “foo”. The directory that “foo” resides in thus would have an entry (“foo”, “10”) that maps the user-readable name to the low-level name. Each entry in a directory refers to either files or other directories. By placing directories within other directories, users are able to build an arbitrary directory tree (or directory hierarchy), under which all files and directories are stored.

/

foo           bar

```text
                      bar.txt         bar              foo
```
```text
bar.txt
```

*Figure 39.1: An Example Directory Tree*

The directory hierarchy starts at a root directory (in UNIX-based systems, the root directory is simply referred to as /) and uses some kind of separator to name subsequent sub-directories until the desired file or directory is named. For example, if a user created a directory foo in the root directory /, and then created a file bar.txt in the directory foo, we could refer to the file by its absolute pathname, which in this case would be /foo/bar.txt. See Figure 39.1 for a more complex directory tree; valid directories in the example are /, /foo, /bar, /bar/bar, /bar/foo and valid files are /foo/bar.txt and /bar/foo/bar.txt. Directories and files can have the same name as long as they are in different locations in the file-system tree (e.g., there are two files named bar.txt in the figure, /foo/bar.txt and /bar/foo/bar.txt).

> **TIP: THINK CAREFULLY ABOUT NAMING**
>
> Naming is an important aspect of computer systems [SK09]. In UNIX systems, virtually everything that you can think of is named through the file system. Beyond just files, devices, pipes, and even processes [K84] can be found in what looks like a plain old file system. This uniformity of naming eases your conceptual model of the system, and makes the system simpler and more modular. Thus, whenever creating a system or interface, think carefully about what names you are using.

You may also notice that the file name in this example often has two parts: bar and txt, separated by a period. The first part is an arbitrary name, whereas the second part of the file name is usually used to indicate the type of the file, e.g., whether it is C code (e.g., .c), or an image (e.g., .jpg), or a music file (e.g., .mp3). However, this is usually just a convention: there is usually no enforcement that the data contained in a file named main.c is indeed C source code.

Thus, we can see one great thing provided by the file system: a convenient way to name all the files we are interested in. Names are important in systems as the first step to accessing any resource is being able to name it. In UNIX systems, the file system thus provides a unified way to access files on disk, USB stick, CD-ROM, many other devices, and in fact many other things, all located under the single directory tree.

## 39.2 The File System Interface

Let’s now discuss the file system interface in more detail. We’ll start with the basics of creating, accessing, and deleting files. You may think this is straightforward, but along the way we’ll discover the mysterious call that is used to remove files, known as unlink(). Hopefully, by the end of this chapter, this mystery won’t be so mysterious to you!

## 39.3 Creating Files

We’ll start with the most basic of operations: creating a file. This can be accomplished with the open system call; by calling open() and passing it the O CREAT flag, a program can create a new file. Here is some example code to create a file called “foo” in the current working directory.

```c
  int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC);
```

The routine open() takes a number of different flags. In this example, the program creates the file (O CREAT), can only write to that file while opened in this manner (O WRONLY), and, if the file already exists, first truncate it to a size of zero bytes thus removing any existing content (O TRUNC).

> **ASIDE: THE CR EA T () SYSTEM CALL**
>
> The older way of creating a file is to call creat(), as follows: int fd = creat("foo");

You can think of creat() as open() with the following flags: O CREAT | O WRONLY | O TRUNC. Because open() can create a file, the usage of creat() has somewhat fallen out of favor (indeed, it could just be implemented as a library call to open()); however, it does hold a special place in UNIX lore. Specifically, when Ken Thompson was asked what he would do differently if he were redesigning UNIX, he replied: “I’d spell creat with an e.”

One important aspect of open() is what it returns: a file descriptor. A file descriptor is just an integer, private per process, and is used in UNIX systems to access files; thus, once a file is opened, you use the file descriptor to read or write the file, assuming you have permission to do so. In this way, a file descriptor is a capability [L84], i.e., an opaque handle that gives you the power to perform certain operations. Another way to think of a file descriptor is as a pointer to an object of type file; once you have such an object, you can call other “methods” to access the file, like read() and write(). We’ll see just how a file descriptor is used below.

## 39.4 Reading and Writing Files

Once we have some files, of course we might like to read or write them. Let’s start by reading an existing file. If we were typing at a command line, we might just use the program cat to dump the contents of the file to the screen.

```text
prompt> echo hello > foo
prompt> cat foo
hello
prompt>
```

In this code snippet, we redirect the output of the program echo to the file foo, which then contains the word “hello” in it. We then use cat to see the contents of the file. But how does the cat program access the file foo?

To find this out, we’ll use an incredibly useful tool to trace the system calls made by a program. On Linux, the tool is called strace; other systems have similar tools (see dtruss on Mac OS X, or truss on some older UNIX variants). What strace does is trace every system call made by a program while it runs, and dump the trace to the screen for you to see.

> **TIP: USE ST RA CE (A ND SIMILAR TOOLS)**
>
> The strace tool provides an awesome way to see what programs are up to. By running it, you can trace which system calls a program makes, see the arguments and return codes, and generally get a very good idea of what is going on. The tool also takes some arguments which can be quite useful. For example, -f follows any fork’d children too; -t reports the time of day at each call; -e trace=open,close,read,write only traces calls to those system calls and ignores all others. There are many more powerful flags — read the man pages and find out how to harness this wonderful tool.

Here is an example of using strace to figure out what cat is doing (some calls removed for readability):

```text
prompt> strace cat foo
...
open("foo", O_RDONLY|O_LARGEFILE)           = 3
read(3, "hello\n", 4096)                    = 6
write(1, "hello\n", 6)                      = 6
hello
read(3, "", 4096)                           = 0
close(3)                                    = 0
...
prompt>
```

The first thing that cat does is open the file for reading. A couple of things we should note about this; first, that the file is only opened for reading (not writing), as indicated by the O RDONLY flag; second, that the 64-bit offset be used (O LARGEFILE); third, that the call to open() succeeds and returns a file descriptor, which has the value of 3.

Why does the first call to open() return 3, not 0 or perhaps 1 as you might expect? As it turns out, each running process already has three files open, standard input (which the process can read to receive input), standard output (which the process can write to in order to dump information to the screen), and standard error (which the process can write error messages to). These are represented by file descriptors 0, 1, and 2, respectively. Thus, when you first open another file (as cat does above), it will almost certainly be file descriptor 3.

After the open succeeds, cat uses the read() system call to repeatedly read some bytes from a file. The first argument to read() is the file descriptor, thus telling the file system which file to read; a process can of course have multiple files open at once, and thus the descriptor enables the operating system to know which file a particular read refers to. The second argument points to a buffer where the result of the read() will be placed; in the system-call trace above, strace shows the results of the read in this spot (“hello”). The third argument is the size of the buffer, which

in this case is 4 KB. The call to read() returns successfully as well, here returning the number of bytes it read (6, which includes 5 for the letters in the word “hello” and one for an end-of-line marker).

At this point, you see another interesting result of the strace: a single call to the write() system call, to the file descriptor 1. As we mentioned above, this descriptor is known as the standard output, and thus is used to write the word “hello” to the screen as the program cat is meant to do. But does it call write() directly? Maybe (if it is highly optimized). But if not, what cat might do is call the library routine printf(); internally, printf() figures out all the formatting details passed to it, and eventually calls write on the standard output to print the results to the screen.

The cat program then tries to read more from the file, but since there are no bytes left in the file, the read() returns 0 and the program knows that this means it has read the entire file. Thus, the program calls close() to indicate that it is done with the file “foo”, passing in the corresponding file descriptor. The file is thus closed, and the reading of it thus complete.

Writing a file is accomplished via a similar set of steps. First, a file is opened for writing, then the write() system call is called, perhaps repeatedly for larger files, and then close(). Use strace to trace writes to a file, perhaps of a program you wrote yourself, or by tracing the dd utility, e.g., dd if=foo of=bar.

## 39.5 Reading And Writing, But Not Sequentially

Thus far, we’ve discussed how to read and write files, but all access has been sequential; that is, we have either read a file from the beginning to the end, or written a file out from beginning to end.

Sometimes, however, it is useful to be able to read or write to a specific offset within a file; for example, if you build an index over a text document, and use it to look up a specific word, you may end up reading from some random offsets within the document. To do so, we will use the lseek() system call. Here is the function prototype:

```c
off_t lseek(int fildes, off_t offset, int whence);
```

The first argument is familiar (a file descriptor). The second argument is the offset, which positions the file offset to a particular location within the file. The third argument, called whence for historical reasons, determines exactly how the seek is performed. From the man page: If whence is SEEK_SET, the offset is set to offset bytes. If whence is SEEK_CUR, the offset is set to its current location plus offset bytes. If whence is SEEK_END, the offset is set to the size of the file plus offset bytes.

As you can tell from this description, for each file a process opens, the OS tracks a “current” offset, which determines where the next read or

> **ASIDE: CALLING LS EE K () DOES NOT PERFORM AD ISK SEEK**
>
> The poorly-named system call lseek() confuses many a student trying to understand disks and how the file systems atop them work. Do not confuse the two! The lseek() call simply changes a variable in OS memory that tracks, for a particular process, at which offset to which its next read or write will start. A disk seek occurs when a read or write issued to the disk is not on the same track as the last read or write, and thus necessitates a head movement. Making this even more confusing is the fact that calling lseek() to read or write from/to random parts of a file, and then reading/writing to those random parts, will indeed lead to more disk seeks. Thus, calling lseek() can certainly lead to a seek in an upcoming read or write, but absolutely does not cause any disk I/O to occur itself.

write will begin reading from or writing to within the file. Thus, part of the abstraction of an open file is that it has a current offset, which is updated in one of two ways. The first is when a read or write of N bytes takes place, N is added to the current offset; thus each read or write implicitly updates the offset. The second is explicitly with lseek, which changes the offset as specified above.

Note that this call lseek() has nothing to do with the seek operation of a disk, which moves the disk arm. The call to lseek() simply changes the value of a variable within the kernel; when the I/O is performed, depending on where the disk head is, the disk may or may not perform an actual seek to fulfill the request.

## 39.6 Writing Immediately with fsync()

Most times when a program calls write(), it is just telling the file system: please write this data to persistent storage, at some point in the future. The file system, for performance reasons, will buffer such writes in memory for some time (say 5 seconds, or 30); at that later point in time, the write(s) will actually be issued to the storage device. From the perspective of the calling application, writes seem to complete quickly, and only in rare cases (e.g., the machine crashes after the write() call but before the write to disk) will data be lost.

However, some applications require something more than this eventual guarantee. For example, in a database management system (DBMS), development of a correct recovery protocol requires the ability to force writes to disk from time to time.

To support these types of applications, most file systems provide some additional control APIs. In the UNIX world, the interface provided to applications is known as fsync(int fd). When a process calls fsync() for a particular file descriptor, the file system responds by forcing all dirty (i.e., not yet written) data to disk, for the file referred to by the specified

file descriptor. The fsync() routine returns once all of these writes are complete.

Here is a simple example of how to use fsync(). The code opens the file foo, writes a single chunk of data to it, and then calls fsync() to ensure the writes are forced immediately to disk. Once the fsync() returns, the application can safely move on, knowing that the data has been persisted (if fsync() is correctly implemented, that is).

```c
  int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC);
  assert(fd > -1);
  int rc = write(fd, buffer, size);
  assert(rc == size);
  rc = fsync(fd);
  assert(rc == 0);
```

Interestingly, this sequence does not guarantee everything that you might expect; in some cases, you also need to fsync() the directory that contains the file foo. Adding this step ensures not only that the file itself is on disk, but that the file, if newly created, also is durably a part of the directory. Not surprisingly, this type of detail is often overlooked, leading to many application-level bugs [P+13].

## 39.7 Renaming Files

Once we have a file, it is sometimes useful to be able to give a file a different name. When typing at the command line, this is accomplished with mv command; in this example, the file foo is renamed bar:

```text
prompt> mv foo bar
```

Using strace, we can see that mv uses the system call rename(char *old, char *new), which takes precisely two arguments: the original name of the file (old) and the new name (new).

One interesting guarantee provided by the rename() call is that it is

```c
(usually) implemented as an atomic call with respect to system crashes;
```

if the system crashes during the renaming, the file will either be named the old name or the new name, and no odd in-between state can arise. Thus, rename() is critical for supporting certain kinds of applications that require an atomic update to file state.

Let’s be a little more specific here. Imagine that you are using a file editor (e.g., emacs), and you insert a line into the middle of a file. The file’s name, for the example, is foo.txt. The way the editor might update the file to guarantee that the new file has the original contents plus the line inserted is as follows (ignoring error-checking for simplicity):

```c
int fd = open("foo.txt.tmp", O_WRONLY|O_CREAT|O_TRUNC);
write(fd, buffer, size); // write out new version of file
fsync(fd);
close(fd);
rename("foo.txt.tmp", "foo.txt");
```

What the editor does in this example is simple: write out the new version of the file under a temporary name (foo.txt.tmp), force it to disk with fsync(), and then, when the application is certain the new file metadata and contents are on the disk, rename the temporary file to the original file’s name. This last step atomically swaps the new file into place, while concurrently deleting the old version of the file, and thus an atomic file update is achieved.

## 39.8 Getting Information About Files

Beyond file access, we expect the file system to keep a fair amount of information about each file it is storing. We generally call such data about files metadata. To see the metadata for a certain file, we can use the stat() or fstat() system calls. These calls take a pathname (or file descriptor) to a file and fill in a stat structure as seen here:

```c
struct stat {
    dev_t     st_dev;     /* ID of device containing file */
    ino_t     st_ino;     /* inode number */
    mode_t    st_mode;    /* protection */
    nlink_t   st_nlink;   /* number of hard links */
    uid_t     st_uid;     /* user ID of owner */
    gid_t     st_gid;     /* group ID of owner */
    dev_t     st_rdev;    /* device ID (if special file) */
    off_t     st_size;    /* total size, in bytes */
    blksize_t st_blksize; /* blocksize for filesystem I/O */
    blkcnt_t st_blocks; /* number of blocks allocated */
    time_t    st_atime;   /* time of last access */
    time_t    st_mtime;   /* time of last modification */
    time_t    st_ctime;   /* time of last status change */
};
```

You can see that there is a lot of information kept about each file, including its size (in bytes), its low-level name (i.e., inode number), some ownership information, and some information about when the file was accessed or modified, among other things. To see this information, you can use the command line tool stat:

```text
prompt> echo hello > file
prompt> stat file
  File: ‘file’
  Size: 6 Blocks: 8          IO Block: 4096    regular file
Device: 811h/2065d Inode: 67158084    Links: 1
```

Access: (0640/-rw-r-----) Uid: (30686/ remzi) Gid: (30686/ remzi) Access: 2011-05-03 15:50:20.157594748 -0500 Modify: 2011-05-03 15:50:20.157594748 -0500 Change: 2011-05-03 15:50:20.157594748 -0500

As it turns out, each file system usually keeps this type of information in a structure called an inode1 . We’ll be learning a lot more about inodes when we talk about file system implementation. For now, you should just think of an inode as a persistent data structure kept by the file system that has information like we see above inside of it.

## 39.9 Removing Files

At this point, we know how to create files and access them, either sequentially or not. But how do you delete files? If you’ve used UNIX, you probably think you know: just run the program rm. But what system call does rm use to remove a file?

Let’s use our old friend strace again to find out. Here we remove that pesky file “foo”:

```text
prompt> strace rm foo
...
unlink("foo")                                        = 0
...
```

We’ve removed a bunch of unrelated cruft from the traced output, leaving just a single call to the mysteriously-named system call unlink(). As you can see, unlink() just takes the name of the file to be removed, and returns zero upon success. But this leads us to a great puzzle: why is this system call named “unlink”? Why not just “remove” or “delete”. To understand the answer to this puzzle, we must first understand more than just files, but also directories.

## 39.10 Making Directories

Beyond files, a set of directory-related system calls enable you to make, read, and delete directories. Note you can never write to a directory directly; because the format of the directory is considered file system metadata, you can only update a directory indirectly by, for example, creating files, directories, or other object types within it. In this way, the file system makes sure that the contents of the directory always are as expected.

To create a directory, a single system call, mkdir(), is available. The eponymous mkdir program can be used to create such a directory. Let’s take a look at what happens when we run the mkdir program to make a simple directory called foo:

```text
prompt> strace mkdir foo
...
mkdir("foo", 0777)                                   = 0
...
prompt>
```

Some file systems call these structures similar, but slightly different, names, such as dnodes; the basic idea is similar however.

> **TIP: BE WARY OF POWERFUL COMMANDS**
>
> The program rm provides us with a great example of powerful commands, and how sometimes too much power can be a bad thing. For example, to remove a bunch of files at once, you can type something like: prompt> rm *

where the * will match all files in the current directory. But sometimes you want to also delete the directories too, and in fact all of their contents. You can do this by telling rm to recursively descend into each directory, and remove its contents too:

```text
prompt> rm -rf *
```

Where you get into trouble with this small string of characters is when you issue the command, accidentally, from the root directory of a file system, thus removing every file and directory from it. Oops! Thus, remember the double-edged sword of powerful commands; while they give you the ability to do a lot of work with a small number of keystrokes, they also can quickly and readily do a great deal of harm.

When such a directory is created, it is considered “empty”, although it does have a bare minimum of contents. Specifically, an empty directory has two entries: one entry that refers to itself, and one entry that refers to its parent. The former is referred to as the “.” (dot) directory, and the latter as “..” (dot-dot). You can see these directories by passing a flag (-a) to the program ls:

```text
prompt> ls -a
./ ../
prompt> ls -al
total 8
drwxr-x--- 2 remzi remzi     6 Apr 30 16:17 ./
drwxr-x--- 26 remzi remzi 4096 Apr 30 16:17 ../
39.11 Reading Directories
```

Now that we’ve created a directory, we might wish to read one too. Indeed, that is exactly what the program ls does. Let’s write our own little tool like ls and see how it is done.

Instead of just opening a directory as if it were a file, we instead use a new set of calls. Below is an example program that prints the contents of a directory. The program uses three calls, opendir(), readdir(), and closedir(), to get the job done, and you can see how simple the interface is; we just use a simple loop to read one directory entry at a time, and print out the name and inode number of each file in the directory.

```c
int main(int argc, char *argv[]) {
    DIR *dp = opendir(".");
    assert(dp != NULL);
    struct dirent *d;
    while ((d = readdir(dp)) != NULL) {
        printf("%d %s\n", (int) d->d_ino, d->d_name);
    }
    closedir(dp);
    return 0;
}
```

The declaration below shows the information available within each directory entry in the struct dirent data structure:

```c
struct dirent {
    char           d_name[256]; /* filename */
    ino_t          d_ino;       /* inode number */
    off_t          d_off;       /* offset to the next dirent */
    unsigned short d_reclen;    /* length of this record */
    unsigned char d_type;       /* type of file */
};
```

Because directories are light on information (basically, just mapping the name to the inode number, along with a few other details), a program may want to call stat() on each file to get more information on each, such as its length or other detailed information. Indeed, this is exactly what ls does when you pass it the -l flag; try strace on ls with and without that flag to see for yourself.

## 39.12 Deleting Directories

Finally, you can delete a directory with a call to rmdir() (which is used by the program of the same name, rmdir). Unlike file deletion, however, removing directories is more dangerous, as you could potentially delete a large amount of data with a single command. Thus, rmdir() has the requirement that the directory be empty (i.e., only has “.” and “..” entries) before it is deleted. If you try to delete a non-empty directory, the call to rmdir() simply will fail.

## 39.13 Hard Links

We now come back to the mystery of why removing a file is performed via unlink(), by understanding a new way to make an entry in the file system tree, through a system call known as link(). The link() system call takes two arguments, an old pathname and a new one; when you “link” a new file name to an old one, you essentially create another way to refer to the same file. The command-line program ln is used to do this, as we see in this example:

```text
prompt> echo hello > file
prompt> cat file
hello
prompt> ln file file2
prompt> cat file2
hello
```

Here we created a file with the word “hello” in it, and called the file file2 . We then create a hard link to that file using the ln program. After this, we can examine the file by either opening file or file2.

The way link works is that it simply creates another name in the directory you are creating the link to, and refers it to the same inode number

```c
(i.e., low-level name) of the original file. The file is not copied in any way;
```

rather, you now just have two human names (file and file2) that both refer to the same file. We can even see this in the directory itself, by printing out the inode number of each file:

```text
prompt> ls -i file file2
67158084 file
67158084 file2
prompt>
```

By passing the -i flag to ls, it prints out the inode number of each file (as well as the file name). And thus you can see what link really has done: just make a new reference to the same exact inode number (67158084 in this example).

By now you might be starting to see why unlink() is called unlink(). When you create a file, you are really doing two things. First, you are making a structure (the inode) that will track virtually all relevant information about the file, including its size, where its blocks are on disk, and so forth. Second, you are linking a human-readable name to that file, and putting that link into a directory.

After creating a hard link to a file, to the file system, there is no difference between the original file name (file) and the newly created file name (file2); indeed, they are both just links to the underlying metadata about the file, which is found in inode number 67158084.

Thus, to remove a file from the file system, we call unlink(). In the example above, we could for example remove the file named file, and still access the file without difficulty:

```text
prompt> rm file
removed ‘file’
prompt> cat file2
hello
```

The reason this works is because when the file system unlinks file, it checks a reference count within the inode number. This reference count

Note how creative the authors of this book are. We also used to have a cat named “Cat” (true story). However, she died, and we now have a hamster named “Hammy.” Update: Hammy is now dead too. The pet bodies are piling up.

(sometimes called the link count) allows the file system to track how many different file names have been linked to this particular inode. When unlink() is called, it removes the “link” between the human-readable name (the file that is being deleted) to the given inode number, and decrements the reference count; only when the reference count reaches zero does the file system also free the inode and related data blocks, and thus truly “delete” the file.

You can see the reference count of a file using stat() of course. Let’s see what it is when we create and delete hard links to a file. In this example, we’ll create three links to the same file, and then delete them. Watch the link count!

```text
prompt> echo hello > file
prompt> stat file
... Inode: 67158084    Links: 1 ...
prompt> ln file file2
prompt> stat file
... Inode: 67158084    Links: 2 ...
prompt> stat file2
... Inode: 67158084    Links: 2 ...
prompt> ln file2 file3
prompt> stat file
... Inode: 67158084    Links: 3 ...
prompt> rm file
prompt> stat file2
... Inode: 67158084    Links: 2 ...
prompt> rm file2
prompt> stat file3
... Inode: 67158084    Links: 1 ...
prompt> rm file3
39.14       Symbolic Links
```

There is one other type of link that is really useful, and it is called a symbolic link or sometimes a soft link. As it turns out, hard links are somewhat limited: you can’t create one to a directory (for fear that you will create a cycle in the directory tree); you can’t hard link to files in other disk partitions (because inode numbers are only unique within a particular file system, not across file systems); etc. Thus, a new type of link called the symbolic link was created.

To create such a link, you can use the same program ln, but with the -s flag. Here is an example:

```text
prompt> echo hello > file
prompt> ln -s file file2
prompt> cat file2
hello
```

As you can see, creating a soft link looks much the same, and the original file can now be accessed through the file name file as well as the symbolic link name file2.

However, beyond this surface similarity, symbolic links are actually quite different from hard links. The first difference is that a symbolic link is actually a file itself, of a different type. We’ve already talked about regular files and directories; symbolic links are a third type the file system knows about. A stat on the symlink reveals all:

```text
prompt> stat file
 ... regular file ...
prompt> stat file2
 ... symbolic link ...
```

Running ls also reveals this fact. If you look closely at the first character of the long-form of the output from ls, you can see that the first character in the left-most column is a - for regular files, a d for directories, and an l for soft links. You can also see the size of the symbolic link (4 bytes in this case), as well as what the link points to (the file named file).

```text
prompt> ls -al
drwxr-x--- 2 remzi remzi    29 May       3 19:10 ./
```

drwxr-x--- 27 remzi remzi 4096 May       3 15:14 ../

```text
-rw-r----- 1 remzi remzi     6 May       3 19:10 file
lrwxrwxrwx 1 remzi remzi     4 May       3 19:10 file2 -> file
```

The reason that file2 is 4 bytes is because the way a symbolic link is formed is by holding the pathname of the linked-to file as the data of the link file. Because we’ve linked to a file named file, our link file file2 is small (4 bytes). If we link to a longer pathname, our link file would be bigger:

```text
prompt> echo hello > alongerfilename
prompt> ln -s alongerfilename file3
prompt> ls -al alongerfilename file3
```

-rw-r----- 1 remzi remzi 6 May 3 19:17 alongerfilename lrwxrwxrwx 1 remzi remzi 15 May 3 19:17 file3 -> alongerfilename

Finally, because of the way symbolic links are created, they leave the possibility for what is known as a dangling reference:

```text
prompt> echo hello > file
prompt> ln -s file file2
prompt> cat file2
hello
prompt> rm file
prompt> cat file2
cat: file2: No such file or directory
```

As you can see in this example, quite unlike hard links, removing the original file named file causes the link to point to a pathname that no longer exists.

## 39.15 Making and Mounting a File System

We’ve now toured the basic interfaces to access files, directories, and certain types of special types of links. But there is one more topic we should discuss: how to assemble a full directory tree from many underlying file systems. This task is accomplished via first making file systems, and then mounting them to make their contents accessible.

To make a file system, most file systems provide a tool, usually referred to as mkfs (pronounced “make fs”), that performs exactly this task. The idea is as follows: give the tool, as input, a device (such as a disk partition, e.g., /dev/sda1) a file system type (e.g., ext3), and it simply writes an empty file system, starting with a root directory, onto that disk partition. And mkfs said, let there be a file system!

However, once such a file system is created, it needs to be made accessible within the uniform file-system tree. This task is achieved via the mount program (which makes the underlying system call mount() to do the real work). What mount does, quite simply is take an existing directory as a target mount point and essentially paste a new file system onto the directory tree at that point.

An example here might be useful. Imagine we have an unmounted ext3 file system, stored in device partition /dev/sda1, that has the following contents: a root directory which contains two sub-directories, a and b, each of which in turn holds a single file named foo. Let’s say we wish to mount this file system at the mount point /home/users. We would type something like this:

```text
prompt> mount -t ext3 /dev/sda1 /home/users
```

If successful, the mount would thus make this new file system available. However, note how the new file system is now accessed. To look at the contents of the root directory, we would use ls like this:

```text
prompt> ls /home/users/
a b
```

As you can see, the pathname /home/users/ now refers to the root of the newly-mounted directory. Similarly, we could access files a and b with the pathnames /home/users/a and /home/users/b. Finally, the files named foo could be accessed via /home/users/a/foo and /home/users/b/foo. And thus the beauty of mount: instead of having a number of separate file systems, mount unifies all file systems into one tree, making naming uniform and convenient.

To see what is mounted on your system, and at which points, simply run the mount program. You’ll see something like this: /dev/sda1 on / type ext3 (rw) proc on /proc type proc (rw) sysfs on /sys type sysfs (rw) /dev/sda5 on /tmp type ext3 (rw) /dev/sda7 on /var/vice/cache type ext3 (rw) tmpfs on /dev/shm type tmpfs (rw) AFS on /afs type afs (rw)

This crazy mix shows that a whole number of different file systems, including ext3 (a standard disk-based file system), the proc file system (a file system for accessing information about current processes), tmpfs (a file system just for temporary files), and AFS (a distributed file system) are all glued together onto this one machine’s file-system tree.

## 39.16 Summary

The file system interface in UNIX systems (and indeed, in any system) is seemingly quite rudimentary, but there is a lot to understand if you wish to master it. Nothing is better, of course, than simply using it (a lot). So please do so! Of course, read more; as always, Stevens [SR05] is the place to begin.

We’ve toured the basic interfaces, and hopefully understood a little bit about how they work. Even more interesting is how to implement a file system that meets the needs of the API, a topic we will delve into in great detail next.

## References

[K84] “Processes as Files” Tom J. Killian USENIX, June 1984 The paper that introduced the /proc file system, where each process can be treated as a file within a pseudo file system. A clever idea that you can still see in modern UNIX systems.

[L84] “Capability-Based Computer Systems” Henry M. Levy Digital Press, 1984 Available: http://homes.cs.washington.edu/˜levy/capabook An excellent overview of early capability-based systems.

[P+13] “Towards Efficient, Portable Application-Level Consistency” Thanumalayan S. Pillai, Vijay Chidambaram, Joo-Young Hwang, Andrea C. Arpaci-Dusseau, and Remzi H. Arpaci-Dusseau HotDep ’13, November 2013 Our own work that shows how readily applications can make mistakes in committing data to disk; in particular, assumptions about the file system creep into applications and thus make the applications work correctly only if they are running on a specific file system.

[SK09] “Principles of Computer System Design” Jerome H. Saltzer and M. Frans Kaashoek Morgan-Kaufmann, 2009 This tour de force of systems is a must-read for anybody interested in the field. It’s how they teach systems at MIT. Read it once, and then read it a few more times to let it all soak in.

[SR05] “Advanced Programming in the UNIX Environment” W. Richard Stevens and Stephen A. Rago Addison-Wesley, 2005 We have probably referenced this book a few hundred thousand times. It is that useful to you, if you care to become an awesome systems programmer.

## Homework

In this homework, we’ll just familiarize ourselves with how the APIs described in the chapter work. To do so, you’ll just write a few different programs, mostly based on various UNIX utilities.

Questions 1. Stat: Write your own version of the command line program stat, which simply calls the stat() system call on a given file or directory. Print out file size, number of blocks allocated, reference (link) count, and so forth. What is the link count of a directory, as the number of entries in the directory changes? Useful interfaces: stat() 2. List Files: Write a program that lists files in the given directory. When called without any arguments, the program should just print the file names. When invoked with the -l flag, the program should print out information about each file, such as the owner, group, permissions, and other information obtained from the stat() system call. The program should take one additional argument, which is the directory to read, e.g., myls -l directory. If no directory is given, the program should just use the current working directory. Useful interfaces: stat(), opendir(), readdir(), getcwd(). 3. Tail: Write a program that prints out the last few lines of a file. The program should be efficient, in that it seeks to near the end of the file, reads in a block of data, and then goes backwards until it finds the requested number of lines; at this point, it should print out those lines from beginning to the end of the file. To invoke the program, one should type: mytail -n file, where n is the number of lines at the end of the file to print. Useful interfaces: stat(), lseek(), open(), read(), close(). 4. Recursive Search: Write a program that prints out the names of each file and directory in the file system tree, starting at a given point in the tree. For example, when run without arguments, the program should start with the current working directory and print its contents, as well as the contents of any sub-directories, etc., until the entire tree, root at the CWD, is printed. If given a single argument (of a directory name), use that as the root of the tree instead. Refine your recursive search with more fun options, similar to the powerful find command line tool. Useful interfaces: you figure it out.
