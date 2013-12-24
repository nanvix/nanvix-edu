/*
 * Copyright(C) 2011-2014 Pedro H. Penna <pedrohenriquepenna@gmail.com>
 * 
 * sys/open.c - open() system call implementation.
 */

#include <nanvix/const.h>
#include <nanvix/dev.h>
#include <nanvix/fs.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>

/*
 * Returns access permissions.
 */
#define PERM(o)                                        \
	((ACCMODE(o) == O_RDWR) ? (MAY_READ | MAY_WRITE) : \
	(ACCMODE(o) == O_WRONLY) ? MAY_WRITE :             \
	MAY_READ | (o & O_TRUNC) ? MAY_WRITE : 0)          \

/*
 * Creates a file.
 */
PRIVATE struct inode *do_creat
(struct inode *d, const char *name, mode_t mode, int oflag)
{
	struct inode *i;
	
	/* Not asked to create file. */
	if (!(oflag & O_CREAT))
	{
		curr_proc->errno = -ENOENT;
		return (NULL);
	}
		
	/* Not allowed to write in parent directory. */
	if (!permission(d->mode, d->uid, d->gid, curr_proc, MAY_WRITE, 0))
	{
		curr_proc->errno = -EACCES;
		return (NULL);
	}
		
	i = inode_alloc(i->sb);
	
	/* Failed to allocate inode. */
	if (i == NULL)
		return (NULL);
		
	i->mode = (mode & MAY_ALL & ~curr_proc->umask) | S_IFREG;

	/* Failed to add directory entry. */
	if (dir_add(d, i, name))
	{
		inode_put(i);
		return (NULL);
	}
		
	inode_put(d);
	inode_unlock(i);
	
	return (i);
}

/*
 * Opens a file.
 */
PRIVATE struct inode *do_open(const char *path, int oflag, mode_t mode)
{
	int err;              /* Error?               */
	const char *name;     /* File name.           */
	struct inode *dinode; /* Directory's inode.   */
	ino_t num;            /* File's inode number. */
	dev_t dev;            /* File's device.       */
	struct inode *i;      /* File's inode.        */
	
	dinode = inode_dname(path, &name);
	
	/* Failed to get directory. */
	if (dinode == NULL)
		return (NULL);
	
	num = dir_search(dinode, name);
	
	/* File does not exist. */
	if (num == INODE_NULL)
	{
		i = do_creat(dinode, name, mode, oflag);
		
		/* Failed to create inode. */
		if (i == NULL)
		{
			inode_put(dinode);
			return (NULL);
		}
			
		return (i);
	}
	
	num = dinode->dev;
	inode_put(dinode);
	
	/* File already exists. */
	if (oflag & O_EXCL)
	{
		curr_proc->errno = -EEXIST;
		return (NULL);
	}
	
	i = inode_get(dev, num);
	
	/* Failed to get inode. */
	if (i == NULL)
		return (NULL);
	
	
	/* Not allowed. */
	if (!permission(i->mode, i->uid, i->gid, curr_proc, PERM(oflag), 0))
	{
		inode_put(i);
		curr_proc->errno = -EACCES;
		return (NULL);
	}
	
	/* Character special file. */
	if (S_ISCHR(i->mode))
	{
		err = cdev_open(i->dev);
		
		/* Failed to open character device. */
		if (err)
		{
			inode_put(i);
			curr_proc->errno = err;
			return (NULL);
		}
	}
	
	/* Block special file. */
	else if (S_ISBLK(i->mode))
	{
		inode_put(i);
		curr_proc->errno = -ENOTSUP;
		return (NULL);
	}
	
	/* Pipe file. */
	else if (S_ISFIFO(i->mode))
	{
		inode_put(i);
		curr_proc->errno = -ENOTSUP;
		return (NULL);
	}
	
	/* Regular file. */
	else if (S_ISREG(i->mode))
	{
		/* Truncate file. */
		if (oflag & O_TRUNC)
			inode_truncate(i);
	}
	
	/* Directory. */
	else if (S_ISDIR(i->mode))
	{
		/* Directories are not writable. */
		if ((ACCMODE(oflag) == O_WRONLY) || (ACCMODE(oflag) == O_RDWR))
		{
			inode_put(i);
			curr_proc->errno = -EISDIR;
			return (NULL);
		}
	}
	
	inode_unlock(i);
	
	return (i);
}

/*
 * Opens a file.
 */
PUBLIC int sys_open(const char *path, int oflag, mode_t mode)
{
	int fd;              /* File descriptor.  */
	struct file *f;      /* File.             */
	struct inode *i; /* Underlying inode. */

	/* Get empty file descriptor. */
	for (fd = 0; fd < OPEN_MAX; fd++)
	{
		/* Found. */
		if (curr_proc->ofiles[fd] == NULL)
			break;
	}
	
	/* Too many opened files. */
	if (fd >= OPEN_MAX)
		return (-EMFILE);

	/* Get file table entry. */
	for (f = &filetab[0]; f < &filetab[NR_FILES]; f++)
	{
		/* Found. */
		if (f->count == 0)
			break;
	}

	/* Too many files open in the system. */
	if (f >= &filetab[NR_FILES])
		return (-ENFILE);
	
	/* Increment reference count before actually opening
	 * the file because we can sleep below and another process
	 * may want to use this file table entry also.  */	
	f->count = 1;

	i = do_open(path, oflag, mode);	
	
	/* Failed to open file*/
	if (i == NULL)
	{
		f->count = 0;
		return (curr_proc->errno);
	}
	
	/* Initialize file. */
	f->oflag = (((oflag & O_ACCMODE) == O_ACCMODE)) ? oflag & ~O_ACCMODE :oflag;
	f->pos = 0;
	f->inode = i;
	
	curr_proc->ofiles[fd] = f;
	curr_proc->close &= ~(1 << fd);

	return (fd);
}