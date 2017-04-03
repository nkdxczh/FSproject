# FSproject

Description:

This code takes the second parameter (the first parameter is mount point) as the path of recovery file. If the second parameter exists, the code will automatically recover the old filesystem in mount point when  we install fuse and save the current file system into recovery file when we unmount fuse. The recovery file is a binary file.

The code supprot directory commands.

The new file system save every file's data in a block linked list in order to support big file. The number of blocks would automatically increase if there's no more space for appending. Every block's size is set to 20 charaters.

Make & Run:

Make: make in root folder

Run: ./run.sh. This shell script takes tmp directory as mount point and save.data file as recover file.

Unmount: ./destory. This shell only stops the fuse and empty tmp.
