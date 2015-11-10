# sockperf
Socket performance comparison tool.
Because folks like to write how much faster than loopback are UNIX domain sockets, but nobody can provide solid numbers under most common circumstances. Now you can see it for yourself.

# Notice
This is only a quick and dirty experiment so anyone interested can evaluate it and see whether implementing AF_UNIX (AF_LOCAL) sockets in their software makes sense. Nevertheless, any bug reports are welcome.

# How to use

1. cd <source dir>
2. make
3. Run spsrv:

   ./spsrv &
   
4. Run spcli:

   ./spcli
   
5. Repeat step 4 if necessary
6. fg
7. <ctrl+c> to kill spsrv