/**
 *  This code is based on an example file system from
 *    http://www.prism.uvsq.fr/~ode/in115/system.html
 *
 *  That site is now unreachable, but the presentation that accompanied that
 *  code is now on CourseSite.
 *
 *  Another useful tutorial is at
 *    http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
 *
 *  Notes:
 *
 *  Fuse is not quite POSIX, but the function names usually have a good
 *  definition via 'man 3'
 *
 *  Missing functions and features:
 *
 *    Directories are not supported:
 *      mkdir, rmdir, opendir, releasedir, fsyncdir
 *
 *    Links are not supported:
 *      readlink, symlink, link
 *
 *    Permission support is limited:
 *      chmod, chown
 *
 *    Other unimplemented functions:
 *      statfs, flush, release, fsync, access, create, ftruncate, fgetattr
 *      (some of these are necessary!)
 *
 *    Files have fixed max length, which isn't a constant in the code
 *
 *    There is no persistence... if you unmount and remount, the files are
 *      gone!
 *
 *  Known bugs:
 *    Permission errors when moving out of and then back into the filesystem
 *
 *    Mountpoint has wrong permissions, due to a bug hidden in one of the
 *    functions
 *
 *    Root should be part of fuse_context
 *
 *    Not really working with file handles yet
 */

#include <fuse.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <vector>
#include <string>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>

using std::string;
using std::vector;
using std::cerr;
using std::endl;

// Returns a pointer to the first character of the last directory entry
// in the given path.
//
// [mfs] need to stop needing this...
const char* get_filename (const char* path)
{
    const char* p;
    if ((p = strrchr(path,'/')) == 0)
        return 0;
    return p+1;
}
/**
 *  Helper function for managing permissions
 *
 *  [mfs] this might not always be doing the right thing ;)
 */
mode_t my_rights(struct stat* stbuf, uid_t uid, gid_t gid)
{
    mode_t rights = 0;
    rights |= stbuf->st_mode & 7;
    if (stbuf->st_uid == uid)
        rights |= (stbuf->st_mode >> 6) & 7;
    if (stbuf->st_gid == gid)
        rights |= (stbuf->st_mode >> 3) & 7;
    return rights;
}

/**
 *  Describe a file
 */
struct file_t
{
    string name;
    struct stat stbuf;
    char data[65536]; // this is not an ideal way of storing data!
};

/**
 *  Describe a directory
 */
struct dir_t
{
    string name;
    vector<file_t> files;
    struct stat stbuf;

    /**
     *  Helper... find a file by name
     */
    file_t* find_file(string name)
    {
        string tmp = get_filename(name.c_str());
        for (unsigned i = 0; i < files.size(); ++i)
            if (files[i].name == tmp)
                return &files[i];
        return NULL;
    }

    /**
     *  [mfs] no warning when file not found...
     */
    void erase_file(string name)
    {
        string tmp = get_filename(name.c_str());
        for (unsigned i = 0; i < files.size(); ++i) {
            if (files[i].name == tmp) {
                files.erase(files.begin() + i);
                return;
            }
        }
    }
};

/**
 *  The root directory of our filesystem
 */
dir_t root;

/**
 *  Get attributes
 */
int myfs_getattr(const char* path, struct stat* stbuf)
{
    if (string(path) == "/") {
        memcpy(stbuf, &root.stbuf, sizeof(struct stat));
    }
    else {
        file_t* file = root.find_file(path);
        if (NULL == file)
            return -ENOENT;
        memcpy(stbuf, &file->stbuf, sizeof(struct stat));
    }
    return 0;
}

/**
 *  Man 3 truncate suggests this is not a conforming implementation
 */
int myfs_truncate(const char* path, off_t size)
{
    if (size > 65536)
        return -EIO;

    file_t* file = root.find_file(path);
    if (file == NULL)
        return -ENOENT;

    fuse_context* context = fuse_get_context();
    mode_t rights = my_rights(&file->stbuf, context->uid, context->gid);
    if (!(rights & 2))
        return -EACCES;

    file->stbuf.st_size = 0;
    return 0;
}

/**
 *  Rename: not safe once we have more folders
 */
int myfs_rename(const char* from, const char* to)
{
    fuse_context* context;
    file_t* file = root.find_file(from);
    if (file == NULL)
        return -ENOENT;
    context = fuse_get_context();
    if (file->stbuf.st_uid != context->uid)
        return -EACCES;

    file->name = get_filename(to);
    return 0;
}

/**
 *  This won't be correct once hard links are supported
 */
int myfs_unlink(const char* path)
{
    fuse_context* context;
    file_t* file;
    file = root.find_file(path);
    if (file == NULL)
        return -ENOENT;
    context = fuse_get_context();
    if (file->stbuf.st_uid != context->uid)
        return -EACCES;
    root.erase_file(path);
    return 0;
}

/**
 *  Example of how to set time
 */
int myfs_utime(const char* path, struct utimbuf* buf)
{
    fuse_context* context;
    file_t* file;

    file = root.find_file(path);
    if (file == NULL)
        return -ENOENT;
    context = fuse_get_context();
    if (buf != 0) {
        if (context->uid != 0 && file->stbuf.st_uid != context->uid)
            return -EPERM;
        file->stbuf.st_atime = file->stbuf.st_mtime = time(0);
    }
    else {
        mode_t rights = my_rights(&file->stbuf, context->uid, context->gid);
        if (context->uid != 0 && file->stbuf.st_uid != context->uid
            && !(rights & 2))
        {
            return -EACCES;
        }
        file->stbuf.st_atime = buf->actime;
        file->stbuf.st_mtime = buf->modtime;
    }
    return 0;
}

/**
 *  Write to a file
 */
int myfs_write(const char* path, const char* buf, size_t size,
               off_t offset, fuse_file_info* fi)
{
    file_t* file = root.find_file(path);
    if (offset+size > 65536)
        return -EIO;
    memcpy(&file->data[offset], buf, size);
    int diff = offset+size-file->stbuf.st_size;
    if (diff > 0) {
        file->stbuf.st_size += diff;
        file->stbuf.st_blocks = 1;
    }
    return size;
}

/**
 *  Read from a file
 */
int myfs_read(const char* path, char* buf, size_t size,
              off_t offset, fuse_file_info* fi)
{
    file_t* file = root.find_file(path);
    if ((offset + size) > 65536)
        return -EIO;
    memcpy(buf, &file->data[offset], size);
    return size;
}

/**
 *  Make a new entry in the filesystem
 */
int myfs_mknod(const char* path, mode_t mode, dev_t dev)
{
    fuse_context* context = fuse_get_context();
    file_t* newfile = new file_t();
    newfile->name = get_filename(path);
    memset(&newfile->stbuf, 0, sizeof(struct stat));
    newfile->stbuf.st_mode = mode;
    newfile->stbuf.st_dev = dev;
    newfile->stbuf.st_nlink = 1;
    newfile->stbuf.st_atime = newfile->stbuf.st_mtime =
        newfile->stbuf.st_ctime = time(0);
    newfile->stbuf.st_uid = context->uid;
    newfile->stbuf.st_gid = context->gid;
    newfile->stbuf.st_blksize = 65536;
    root.files.push_back(*newfile);
    return 0;
}

/**
 *  Return the contents of a directory
 */
int myfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                 off_t offset, fuse_file_info* fi)
{
    // first put in '.' and '..'
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // the only directory is root... otherwise we may need to use fi->fh
    //
    // NB: using C++0x type inference for iterators... w00t!
    for (auto i = root.files.begin(), e = root.files.end(); i != e; ++i) {
        filler(buf, i->name.c_str(), &i->stbuf, 0);
    }
    return 0;
}

/**
 *  Open a file
 */
int myfs_open(const char* path, fuse_file_info* fi)
{
    fuse_context* context;
    file_t* file = root.find_file(path);

    if (file == NULL)
        return -ENOENT;

    context = fuse_get_context();
    if (context->uid != 0) {
        mode_t rights = my_rights(&file->stbuf, context->uid, context->gid);
        if ((fi->flags & O_RDONLY && !(rights & 4)) ||
            (fi->flags & O_WRONLY && !(rights & 2)) ||
            (fi->flags & O_RDWR && ! ((rights&4)&&(rights&2))))
            return -EACCES;
    }

    fi->fh = (unsigned long)file;
    return 0;
}

/**
 *  For debugging, to get you started with figuring out why root permissions
 *  are funny
 */
void info()
{
    uid_t a = getuid(), b = geteuid(), c = getgid(), d = getegid();
    cerr << "uid, euid, gid, egid = "
         << a << ", "
         << b << ", "
         << c << ", "
         << d << endl;
}

/**
 *  Initialization code... this isn't quite correct (have fun :)
 *
 *  Note: we need to set up the root object, so that we can stat it later
 */
void* myfs_init(fuse_conn_info* conn)
{
    info();
    fuse_context* context = fuse_get_context();
    cerr << "contextuid = " << context->uid << endl;
    root.name = "/";
    memset(&root.stbuf, 0, sizeof(struct stat));
    root.stbuf.st_mode = S_IFDIR | 0755;
    root.stbuf.st_nlink = 2;
    root.stbuf.st_uid = context->uid;
    root.stbuf.st_gid = context->gid;
    root.stbuf.st_ctime = root.stbuf.st_mtime = root.stbuf.st_atime = time(0);
    return NULL;
}

/**
 *  When we unmount a filesystem, this gets called.
 */
void myfs_destroy (void* state)
{
}

/**
 *  To configure our filesystem, we need to create a fuse_operations struct
 *  that has pointers to all of the functions that we have created.
 *  Unfortunately, when we do this in C++, we can't use easy struct initializer
 *  lists, so we need to make an initialization function.
 */
fuse_operations myfs_ops;
void pre_init()
{
    myfs_ops.destroy = myfs_destroy;
    myfs_ops.getattr = myfs_getattr;
    myfs_ops.readdir = myfs_readdir;
    myfs_ops.open = myfs_open;
    myfs_ops.init = myfs_init;
    myfs_ops.read = myfs_read;
    myfs_ops.mknod = myfs_mknod;
    myfs_ops.truncate = myfs_truncate;
    myfs_ops.write = myfs_write;
    myfs_ops.utime = myfs_utime;
    myfs_ops.unlink = myfs_unlink;
    myfs_ops.rename = myfs_rename;
    info();
}

/**
 *  Fuse main can do some initialization, but needs to call fuse_main
 *  eventually...
 */
int main(int argc, char* argv[])
{
    pre_init();
    return fuse_main(argc, argv, &myfs_ops, NULL);
}
