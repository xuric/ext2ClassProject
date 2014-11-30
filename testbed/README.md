EXT2 library
============

Created for a class project, this library provides read, write, file-open, file-close, and directory-reading functions for disk images in <a href="http://www.nongnu.org/ext2-doc/ext2.html">EXT 2</a> format, with a block size of *1 Kb*.

Functions provided
==================

setdiskimage
------------

int setdiskimage(char \*path)

Given a file at *path*, open it for reading and writing and pass back
an integer file descriptor.

Currently, only use of only one disk image is supported. Attempts to set a new disk iamge while one has already been set will close the original. There is room for improvement in this area.

*This function does not verify that the file supplied is a valid EXT2 disk iamge.*

This function must be called prior to any call to the below functions. They will gracefully fail if a disk image has not been set.


ext2open
--------

int ext2open(char \*path, int mode)

Given a file at *path*, open it with read/write specification supplied by *mode*.

*mode* can be: 0 (Read Only), 1 (Write Only), 2 (Read/write).

These modes behave like O\_RDONLY, O\_WRONLY, and O\_RDWR from _open_ (see: man 2 open).

The return value is an integer representing a file handle. This number is actually the inode number of the target file.


ext2close
---------

int myclose(int fd)

Given a file descriptor *fd*, close the file; preventing further operations on it.

The function returns 0 on success and -1 on failure. It behaves nearly identically to _close_ (see man 2 close), with the exception being that it does not set *errno* on failure.


ext2read
--------

size\_t ext2read(int fd, void \*buf, size\_t len)

Reads up to *len* bytes from file referenced by *fd*, storing it into buffer pointed to by *buf*. 

*buf* should be initialized and point to a valid memory space sufficient to hold as many as *len* bytes. _ext2read_'s behavior is undefined in such a case.

Return value is the total number of bytes actually read, up to *len* or until the end of file is reached, whichever is smaller.

On error, 0 is returned. *TODO* return -1 instead. 0 COULD be a valid amount.

Unlike _ext2rite_ there are no limitations on file size.


ext2write
---------

size\_t ext2write(int fd, void \*buf, size\_t len)

Writes as many as *len* bytes from *buf* to file referenced by *fd*.

*buf* should be initialized and point to valid memory. _ext2write_'s behavior is undefined in the case that *len* exceeds the amount of memory allocated to *buf*. A segmentation fault is raised if *buf* is unitialized.

_ext2write_ cannot handle writing files larger than 16MB. See: triply-indirect blocks in the Ext 2 specification.

Returns the number of bytes actually witten, or 0 on error.

