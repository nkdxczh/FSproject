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
#include <fstream>

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
 *  Describe a block
 */
const unsigned BLOCK_SIZE = 5;

struct block_t
{
    char data[BLOCK_SIZE];
    block_t* next;
};

/**
 *  Describe a file
 */
struct file_t
{
    string name;
    struct stat stbuf;
    block_t* data;
    unsigned blocks;
    //char data[65536]; // this is not an ideal way of storing data!
};

/**
 *  Describe a directory
 */
struct dir_t
{
    string name;
    dir_t* father;
    vector<file_t> files;
    vector<dir_t> dirs;
    struct stat stbuf;

    /**
     *  Helper... find a file by name
     */
    file_t* find_file(string name)
    {
        string tmp = name.c_str();
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
        string tmp = name.c_str();
        for (unsigned i = 0; i < files.size(); ++i) {
            if (files[i].name == tmp) {
                files.erase(files.begin() + i);
                return;
            }
        }
    }

    /**
     *  Helper... find a dir by name
     */
    dir_t* find_dir(string name)
    {
        string tmp = name.c_str();
        for (unsigned i = 0; i < dirs.size(); ++i)
            if (dirs[i].name == tmp)
                return &dirs[i];
        return NULL;
    }

    void erase_dir(string name)
    {
        string tmp = name.c_str();
        for (unsigned i = 0; i < dirs.size(); ++i) {
            if (dirs[i].name == tmp) {
                dirs.erase(dirs.begin() + i);
                return;
            }
        }
    }
};

/**
 *  The root directory of our filesystem
 */
dir_t root;

char* saveFile;

void save(std::fstream* out, dir_t* dir){
  cerr << "save" << endl;
  for(unsigned i = 0; i < dir->files.size(); i++){
    file_t* file = &dir->files[i];
    *out << 'f' << file->name << '\0';
    out->write((char*)(&file->stbuf), sizeof(file->stbuf));
    block_t* block = file->data;
    for(unsigned j = 0; j < file->blocks; j++){
        *out << "c";
        out->write(block->data, BLOCK_SIZE);
        block = block->next;
    }
    *out << "e";
  }

  for(unsigned i = 0; i < dir->dirs.size(); i++){
    dir_t* temdir = &dir->dirs[i];
    *out << 'd' << temdir->name << '\0';
    out->write((char*)(&temdir->stbuf), sizeof(temdir->stbuf));
    save(out, temdir);
    *out << "E";
  }
}

void recover(std::fstream* in, dir_t* dir){
  cerr << "recover " << dir->name << endl;
  char c;
  file_t temfile;
  dir_t temdir;
  while(in->read(&c,1)){
    cerr << "new: " << c << endl;
    if(c=='f'){
        temfile.name = "";
        in->read(&c,1);
        while(c!='\0'){
          temfile.name += c;
          in->read(&c,1);
        }
        cerr << temfile.name << endl;

        in->read((char*)(&temfile.stbuf), sizeof(temfile.stbuf));

        block_t* block;
        temfile.blocks = 0;
        while(in->read(&c,1)){
            if(c=='e')break;
            temfile.blocks++;
            if(temfile.blocks == 1){
                temfile.data = new block_t();
                block = temfile.data;
            }
            else{
                block->next = new block_t();
                block = block->next;
            }
            in->read((char*)(&block->data), BLOCK_SIZE);
        }
        dir->files.push_back(temfile);
    }

    if(c=='d'){
        temdir.name = "";
        in->read(&c,1);
        while(c!='\0'){
          temdir.name += c;
          in->read(&c,1);
        }
        cerr << temdir.name << endl;

        in->read((char*)(&temdir.stbuf), sizeof(temdir.stbuf));
        recover(in, &temdir);
        dir->dirs.push_back(temdir);
    }

    if(c=='E')return;
  }
}

dir_t* findDir(const char* path){
  if(strlen(path) == 0)return &root;
  char* str =(char *)malloc(strlen(path) + 1);
  strcpy(str,path);
  dir_t* last=&root;
  dir_t* present = &root;
  char * pch;
  string tempath = "/";
  pch = strtok (str,"/");
  while (pch != NULL)
  {
    tempath += pch;
    last = present;
    present = present->find_dir(tempath);
    if(present == NULL)return last;
    pch = strtok (NULL, "/");
  }
  return last;
}

/**
 *  Get attributes
 */
int myfs_getattr(const char* path, struct stat* stbuf)
{
    dir_t* current_dir;
    current_dir = findDir(path);
    if (string(path) == current_dir->name) {
        memcpy(stbuf, &current_dir->stbuf, sizeof(struct stat));
    }
    else {
        file_t* file = current_dir->find_file(path);
        dir_t* dir = current_dir->find_dir(path);
        if (NULL == dir && NULL == file)
            return -ENOENT;
        else if(NULL != dir){
          memcpy(stbuf, &dir->stbuf, sizeof(struct stat));
          cerr << "getattr" << path << "," << current_dir->name << endl;
        }
        else memcpy(stbuf, &file->stbuf, sizeof(struct stat));
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

    dir_t* current_dir;
    current_dir = findDir(path);
    file_t* file = current_dir->find_file(path);
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
    //cerr << "rename" << endl;
    fuse_context* context;
    dir_t* current_dir;
    current_dir = findDir(from);
    file_t* file = current_dir->find_file(from);
    if (file == NULL)
        return -ENOENT;
    context = fuse_get_context();
    if (file->stbuf.st_uid != context->uid)
        return -EACCES;

    file->name = to;
    return 0;
}

/**
 *  This won't be correct once hard links are supported
 */
int myfs_unlink(const char* path)
{
    cerr << "unlink" << path << endl;
    fuse_context* context;
    file_t* file;
    dir_t* current_dir;
    current_dir = findDir(path);
    file = current_dir->find_file(path);
    if (file == NULL)
        return -ENOENT;
    context = fuse_get_context();
    if (file->stbuf.st_uid != context->uid)
        return -EACCES;
    current_dir->erase_file(path);
    return 0;
}

int myfs_rmdir(const char* path)
{
    cerr << "rmdir" << path << endl;
    fuse_context* context;
    dir_t* dir;
    dir_t* current_dir;
    current_dir = findDir(path);
    dir = current_dir->find_dir(path);
    if (dir == NULL)
        return -ENOENT;
    context = fuse_get_context();
    if (dir->stbuf.st_uid != context->uid)
        return -EACCES;
    current_dir->erase_dir(path);
    return 0;
}

/**
 *  Example of how to set time
 */
int myfs_utime(const char* path, struct utimbuf* buf)
{
    fuse_context* context;
    file_t* file;
    dir_t* dir;

    dir_t* current_dir;
    current_dir = findDir(path);
    file = current_dir->find_file(path);
    dir = current_dir->find_dir(path);

    if (file == NULL && dir == NULL)
        return -ENOENT;

    context = fuse_get_context();
    if (buf != 0) {
        if(file != NULL){
            if (context->uid != 0 && file->stbuf.st_uid != context->uid)
                return -EPERM;
            file->stbuf.st_atime = file->stbuf.st_mtime = time(0);
        }
        else{
            if (context->uid != 0 && dir->stbuf.st_uid != context->uid)
                return -EPERM;
            dir->stbuf.st_atime = dir->stbuf.st_mtime = time(0);
        }
    }
    else {
        if(file != NULL){
            mode_t rights = my_rights(&file->stbuf, context->uid, context->gid);
            if (context->uid != 0 && file->stbuf.st_uid != context->uid
                && !(rights & 2))
            {
                return -EACCES;
            }
            file->stbuf.st_atime = buf->actime;
            file->stbuf.st_mtime = buf->modtime;
        }
        else{
            mode_t rights = my_rights(&dir->stbuf, context->uid, context->gid);
            if (context->uid != 0 && dir->stbuf.st_uid != context->uid
                && !(rights & 2))
            {
                return -EACCES;
            }
            dir->stbuf.st_atime = buf->actime;
            dir->stbuf.st_mtime = buf->modtime;
        }
    }
    return 0;
}

/**
 *  Write to a file
 */
int myfs_write(const char* path, const char* buf, size_t size,
               off_t offset, fuse_file_info* fi)
{
    cerr << "write: " << path << endl;
    dir_t* current_dir;
    current_dir = findDir(path);
    file_t* file = current_dir->find_file(path);
    //if (offset > file->blocks * BLOCK_SIZE)
      //  return -EIO;
    
    unsigned begin;
    if(file->data == NULL){
        file->blocks = 1;
        file->data = new block_t();
    }
    block_t* block = file->data;
    for(begin = 0; begin < file->blocks; begin++){
        if(offset - begin * BLOCK_SIZE < BLOCK_SIZE)break;
        if(block->next == NULL){
            file->blocks += 1;
            block->next = new block_t();
        }
        block = block->next;
    }
    offset -= begin * BLOCK_SIZE;

    unsigned current_size = 0;
    while(current_size < size){
        if(offset + size - current_size <= BLOCK_SIZE){
            memcpy(&block->data[offset], buf + current_size, size - current_size);
            break;
        }
        memcpy(&block->data[offset], buf + current_size, BLOCK_SIZE-offset);
        current_size += BLOCK_SIZE-offset;
        offset = 0;
        if(block->next == NULL){
            file->blocks += 1;
            block->next = new block_t();
        }
        block = block->next;
    }
    
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
    cerr << "read: " << path << endl;
    dir_t* current_dir;
    current_dir = findDir(path);
    file_t* file = current_dir->find_file(path);
    cerr << "read: " << offset << "," << size << endl;
    if (offset > file->blocks * BLOCK_SIZE)
        return -EIO;
    unsigned begin;
    block_t* block = file->data;
    for(begin = 0; begin < file->blocks; begin++){
        if(offset - begin * BLOCK_SIZE < BLOCK_SIZE)break;
        block = block->next;
    }
    offset -= begin * BLOCK_SIZE;
    
    unsigned current_size = 0;
    while(begin < file->blocks && current_size < size){
        if(size - current_size + offset < BLOCK_SIZE){
            memcpy(buf + current_size, &block->data[offset], size - current_size);
            break;
        }
        memcpy(buf + current_size, &block->data[offset], BLOCK_SIZE - offset);
        block = block->next;
        begin++;
        current_size += BLOCK_SIZE - offset;
        offset = 0;
    }
    return size;
}

/**
 *  Make a new entry in the filesystem
 */
int myfs_mknod(const char* path, mode_t mode, dev_t dev)
{
    //cerr << "mknod" << endl;
    dir_t* current_dir;
    current_dir = findDir(path);
    fuse_context* context = fuse_get_context();
    file_t* newfile = new file_t();
    newfile->name = path;
    memset(&newfile->stbuf, 0, sizeof(struct stat));
    newfile->stbuf.st_mode = mode;
    newfile->stbuf.st_dev = dev;
    newfile->stbuf.st_nlink = 1;
    newfile->stbuf.st_atime = newfile->stbuf.st_mtime =
        newfile->stbuf.st_ctime = time(0);
    newfile->stbuf.st_uid = context->uid;
    newfile->stbuf.st_gid = context->gid;
    newfile->stbuf.st_blksize = BLOCK_SIZE;
    current_dir->files.push_back(*newfile);
    return 0;
}

int myfs_mkdir (const char* path, mode_t mode){

    /*int res;
    res = mkdir(path, mode);
    if (res == -1)return -errno;
    return 0;*/

    dir_t* current_dir;
    current_dir = findDir(path);
    cerr << "mkdir" << current_dir->name << "|" << path << endl;

    fuse_context* context = fuse_get_context();
    dir_t* newdir = new dir_t();
    newdir->name = path;
    newdir->father = current_dir;
    memset(&newdir->stbuf, 0, sizeof(struct stat));
    cerr << newdir->name << endl;
    newdir->stbuf.st_mode = S_IFDIR | 0755;
    newdir->stbuf.st_nlink = 2;
    newdir->stbuf.st_atime = newdir->stbuf.st_mtime = newdir->stbuf.st_ctime = time(0);
    newdir->stbuf.st_uid = context->uid;
    newdir->stbuf.st_gid = context->gid;
    current_dir->dirs.push_back(*newdir);
    return 0;
}

/**
 *  Return the contents of a directory
 */
int myfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                 off_t offset, fuse_file_info* fi)
{
    dir_t* current_dir;
    current_dir = findDir(path);
    if(string(path) != current_dir->name)current_dir = current_dir->find_dir(path);
    // first put in '.' and '..'
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // the only directory is root... otherwise we may need to use fi->fh
    //
    // NB: using C++0x type inference for iterators... w00t!
    for (auto i = current_dir->files.begin(), e = current_dir->files.end(); i != e; ++i) {
        filler(buf, get_filename(i->name.c_str()), &i->stbuf, 0);
    }
    for (auto i = current_dir->dirs.begin(), e = current_dir->dirs.end(); i != e; ++i) {
        filler(buf, get_filename(i->name.c_str()), &i->stbuf, 0);
    }
    return 0;
}

/**
 *  Open a file
 */
int myfs_open(const char* path, fuse_file_info* fi)
{
    fuse_context* context;
    dir_t* current_dir;
    current_dir = findDir(path);
    file_t* file = current_dir->find_file(path);

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
 *  Open a dir
 */
int myfs_opendir(const char* path, fuse_file_info* fi)
{
    dir_t* current_dir;
    current_dir = findDir(path);
    dir_t* dir;
    fuse_context* context;

    if (string(path) == "/") {
        dir = &root;
    }
    else{
        dir = current_dir->find_dir(path);
    }

    if (dir == NULL)
        return -ENOENT;

    cerr << "opendir" << path << "," << dir->name << endl;

    context = fuse_get_context();
    if (context->uid != 0) {
        mode_t rights = my_rights(&dir->stbuf, context->uid, context->gid);
        if ((fi->flags & O_RDONLY && !(rights & 4)) ||
            (fi->flags & O_WRONLY && !(rights & 2)) ||
            (fi->flags & O_RDWR && ! ((rights&4)&&(rights&2))))
            return -EACCES;
    }

    fi->fh = (unsigned long)dir;

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
    std::fstream in(saveFile,std::ios::in | std::ios::binary );
    recover(&in,&root);
    in.close();
    return NULL;
}

/**
 *  When we unmount a filesystem, this gets called.
 */
void myfs_destroy (void* state)
{
    cerr << "destroy" << endl;
    std::fstream out(saveFile,std::ios_base::out | std::ios_base::binary);
    save(&out,&root);
    out.close();
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
    myfs_ops.opendir = myfs_opendir;
    myfs_ops.init = myfs_init;
    myfs_ops.read = myfs_read;
    myfs_ops.mknod = myfs_mknod;
    myfs_ops.mkdir = myfs_mkdir;
    myfs_ops.truncate = myfs_truncate;
    myfs_ops.write = myfs_write;
    myfs_ops.utime = myfs_utime;
    myfs_ops.unlink = myfs_unlink;
    myfs_ops.rmdir = myfs_rmdir;
    myfs_ops.rename = myfs_rename;
    //info();
}

/**
 *  Fuse main can do some initialization, but needs to call fuse_main
 *  eventually...
 */
int main(int argc, char* argv[])
{
    pre_init();
    argc -= 1;
    saveFile = argv[3];
    return fuse_main(argc, argv, &myfs_ops, NULL);
}
