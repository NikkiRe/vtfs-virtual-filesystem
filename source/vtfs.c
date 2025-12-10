#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dcache.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/fcntl.h>
#include <linux/byteorder/generic.h>
#include "http.h"

#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

#define VTFS_ROOT_INO 100
#define VTFS_MAX_NAME 256

struct vtfs_file {
  struct list_head list;
  ino_t ino;
  umode_t mode;
  char name[VTFS_MAX_NAME];
  struct vtfs_dir* dir_data;
  char* data;
  size_t data_size;
  unsigned int nlink;
};

struct vtfs_dir {
  struct list_head files;
  struct rw_semaphore sem;
};

struct vtfs_fs_info {
  struct vtfs_dir root_dir;
  ino_t next_ino;
  char* token;
  bool use_server;
  struct super_block* sb;
};

struct data_ptr_entry {
  struct list_head list;
  char* data;
};

static struct inode* vtfs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino);
static int vtfs_fill_super(struct super_block* sb, void* data, int silent);
static struct dentry* vtfs_mount(struct file_system_type* fs_type, int flags, const char* token, void* data);
static void vtfs_kill_sb(struct super_block* sb);
static struct dentry* vtfs_lookup(struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag);
static int vtfs_iterate(struct file* filp, struct dir_context* ctx);
static int vtfs_getattr(struct mnt_idmap* idmap, const struct path* path, struct kstat* stat, u32 request_mask, unsigned int flags);
static int vtfs_create(struct mnt_idmap* idmap, struct inode* parent_inode, struct dentry* child_dentry, umode_t mode, bool b);
static int vtfs_unlink(struct inode* parent_inode, struct dentry* child_dentry);
static int vtfs_mkdir(struct mnt_idmap* idmap, struct inode* parent_inode, struct dentry* child_dentry, umode_t mode);
static int vtfs_rmdir(struct inode* parent_inode, struct dentry* child_dentry);
static int vtfs_link(struct dentry* old_dentry, struct inode* parent_dir, struct dentry* new_dentry);
static ssize_t vtfs_read(struct file* filp, char __user* buffer, size_t len, loff_t* offset);
static ssize_t vtfs_write(struct file* filp, const char __user* buffer, size_t len, loff_t* offset);
static int vtfs_open(struct inode* inode, struct file* filp);
static int vtfs_setattr(struct mnt_idmap* idmap, struct dentry* dentry, struct iattr* attr);

static struct inode_operations vtfs_inode_ops;
static struct file_operations vtfs_dir_ops;
static struct file_operations vtfs_file_ops;

static struct vtfs_file* vtfs_find_file(struct vtfs_dir* dir, const char* name);
static struct vtfs_file* vtfs_create_file(struct vtfs_dir* dir, const char* name, umode_t mode, ino_t ino);
static int vtfs_remove_file(struct vtfs_dir* dir, const char* name);
static void vtfs_cleanup_dir(struct vtfs_dir* dir);
static struct vtfs_dir* vtfs_get_dir(struct super_block* sb, ino_t ino);
static struct vtfs_file* vtfs_find_file_by_ino(struct vtfs_dir* dir, ino_t ino);
static struct vtfs_file* vtfs_get_file_by_inode(struct inode* inode);
static void vtfs_update_data_all(struct vtfs_dir* dir, ino_t ino, char* old_data, char* new_data, size_t new_size);

// Server integration functions
static int vtfs_server_create_file(struct vtfs_fs_info* info, ino_t parent_ino, const char* name, umode_t mode, ino_t* out_ino);
static int vtfs_server_write_file(struct vtfs_fs_info* info, ino_t ino, loff_t offset, const char* data, size_t len);
static int vtfs_server_read_file(struct vtfs_fs_info* info, ino_t ino, loff_t offset, size_t len, char* buffer, size_t* out_len);
static int vtfs_server_delete_file(struct vtfs_fs_info* info, ino_t ino);
static int vtfs_server_mkdir(struct vtfs_fs_info* info, ino_t parent_ino, const char* name, umode_t mode, ino_t* out_ino);
static int vtfs_server_rmdir(struct vtfs_fs_info* info, ino_t ino);
static int vtfs_server_link(struct vtfs_fs_info* info, ino_t old_ino, ino_t parent_ino, const char* name, unsigned int* out_nlink);
static int vtfs_server_unlink(struct vtfs_fs_info* info, ino_t ino);
static int vtfs_server_load_files(struct vtfs_fs_info* info, ino_t parent_ino);

static struct file_system_type vtfs_fs_type = {
  .name = "vtfs",
  .mount = vtfs_mount,
  .kill_sb = vtfs_kill_sb,
  .owner = THIS_MODULE,
};

static struct vtfs_file* vtfs_find_file_by_ino(struct vtfs_dir* dir, ino_t ino) {
  struct vtfs_file* file;
  
  if (!dir) return NULL;
  
  down_read(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->ino == ino) {
      up_read(&dir->sem);
      return file;
    }
    if (file->dir_data) {
      struct vtfs_file* found = vtfs_find_file_by_ino(file->dir_data, ino);
      if (found) {
        up_read(&dir->sem);
        return found;
      }
    }
  }
  up_read(&dir->sem);
  return NULL;
}

static struct vtfs_file* vtfs_find_file(struct vtfs_dir* dir, const char* name) {
  struct vtfs_file* file;
  if (!dir) return NULL;
  
  list_for_each_entry(file, &dir->files, list) {
    if (!strcmp(file->name, name)) {
      return file;
    }
  }
  return NULL;
}

static struct vtfs_file* vtfs_create_file(struct vtfs_dir* dir, const char* name, umode_t mode, ino_t ino) {
  struct vtfs_file* file;
  
  if (!dir || !name || strlen(name) >= VTFS_MAX_NAME) {
    return NULL;
  }
  
  down_write(&dir->sem);
  if (vtfs_find_file(dir, name) != NULL) {
    up_write(&dir->sem);
    return NULL;
  }
  
  file = kmalloc(sizeof(struct vtfs_file), GFP_KERNEL);
  if (!file) {
    up_write(&dir->sem);
    return NULL;
  }
  
  INIT_LIST_HEAD(&file->list);
  file->ino = ino;
  file->mode = mode;
  strncpy(file->name, name, VTFS_MAX_NAME - 1);
  file->name[VTFS_MAX_NAME - 1] = '\0';
  file->dir_data = NULL;
  file->data = NULL;
  file->data_size = 0;
  file->nlink = 1;
  
  if (S_ISDIR(mode)) {
    file->dir_data = kmalloc(sizeof(struct vtfs_dir), GFP_KERNEL);
    if (!file->dir_data) {
      kfree(file);
      up_write(&dir->sem);
      return NULL;
    }
    INIT_LIST_HEAD(&file->dir_data->files);
    init_rwsem(&file->dir_data->sem);
  }
  
  list_add_tail(&file->list, &dir->files);
  up_write(&dir->sem);
  
  return file;
}

static int vtfs_remove_file(struct vtfs_dir* dir, const char* name) {
  struct vtfs_file* file;
  
  if (!dir || !name) return -ENOENT;
  
  down_write(&dir->sem);
  file = vtfs_find_file(dir, name);
  if (!file) {
    up_write(&dir->sem);
    return -ENOENT;
  }
  
  list_del(&file->list);
  up_write(&dir->sem);
  
  if (file->dir_data) {
    vtfs_cleanup_dir(file->dir_data);
    kfree(file->dir_data);
  }
  if (file->data) {
    kfree(file->data);
  }
  kfree(file);
  
  return 0;
}

static void vtfs_cleanup_dir(struct vtfs_dir* dir) {
  struct vtfs_file* file;
  struct vtfs_file* tmp;
  struct list_head data_list;
  struct data_ptr_entry* data_entry;
  struct data_ptr_entry* data_tmp;
  char* data_ptr;
  bool found;
  
  if (!dir) return;
  
  INIT_LIST_HEAD(&data_list);
  down_read(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->data) {
      found = false;
      list_for_each_entry(data_entry, &data_list, list) {
        if (data_entry->data == file->data) {
          found = true;
          break;
        }
      }
      if (!found) {
        data_entry = kmalloc(sizeof(struct data_ptr_entry), GFP_KERNEL);
        if (data_entry) {
          data_entry->data = file->data;
          list_add_tail(&data_entry->list, &data_list);
        }
      }
    }
  }
  up_read(&dir->sem);
  
  down_write(&dir->sem);
  list_for_each_entry_safe(file, tmp, &dir->files, list) {
    list_del(&file->list);
    
    if (file->dir_data) {
      vtfs_cleanup_dir(file->dir_data);
      kfree(file->dir_data);
      file->dir_data = NULL;
    }
    
    file->data = NULL;
    
    kfree(file);
  }
  up_write(&dir->sem);
  
  list_for_each_entry_safe(data_entry, data_tmp, &data_list, list) {
    data_ptr = data_entry->data;
    list_del(&data_entry->list);
    kfree(data_entry);
    if (data_ptr) {
      kfree(data_ptr);
    }
  }
}

static struct vtfs_dir* vtfs_get_dir(struct super_block* sb, ino_t ino) {
  struct vtfs_fs_info* info;
  struct vtfs_file* file;
  
  if (!sb) return NULL;
  
  info = sb->s_fs_info;
  if (!info) return NULL;
  
  if (ino == VTFS_ROOT_INO) {
    return &info->root_dir;
  }
  
  file = vtfs_find_file_by_ino(&info->root_dir, ino);
  
  if (file && file->dir_data && S_ISDIR(file->mode)) {
    return file->dir_data;
  }
  
  return NULL;
}

static struct inode* vtfs_get_inode(
  struct super_block* sb, 
  const struct inode* dir, 
  umode_t mode, 
  int i_ino
) {
  struct inode *inode = new_inode(sb);
  if (inode != NULL) {
    inode->i_ino = i_ino;
    inode->i_mode = mode;
    if (dir) {
      inode->i_uid = dir->i_uid;
      inode->i_gid = dir->i_gid;
    } else {
      inode->i_uid = GLOBAL_ROOT_UID;
      inode->i_gid = GLOBAL_ROOT_GID;
    }
    if (S_ISREG(mode)) {
      inode->i_size = 0;
    }
  }
  return inode;
}

static struct dentry* vtfs_lookup(
  struct inode* parent_inode,  
  struct dentry* child_dentry, 
  unsigned int flag            
) {
  struct vtfs_dir* dir;
  struct vtfs_file* file;
  struct inode* inode;
  const char* name = child_dentry->d_name.name;
  
  if (!parent_inode || !child_dentry) {
    return NULL;
  }
  
  dir = vtfs_get_dir(parent_inode->i_sb, parent_inode->i_ino);
  if (!dir) {
    return NULL;
  }
  
  down_read(&dir->sem);
  file = vtfs_find_file(dir, name);
  if (!file) {
    up_read(&dir->sem);
    return NULL;
  }
  
  umode_t file_mode = file->mode;
  ino_t file_ino = file->ino;
  size_t file_data_size = file->data_size;
  up_read(&dir->sem);
  
  inode = vtfs_get_inode(parent_inode->i_sb, parent_inode, file_mode, file_ino);
  if (!inode) {
    return NULL;
  }
  
  if (S_ISREG(file_mode)) {
    inode->i_size = file_data_size;
  }
  
  inode->i_op = &vtfs_inode_ops;
  if (S_ISDIR(file_mode)) {
    inode->i_fop = &vtfs_dir_ops;
  } else {
    inode->i_fop = &vtfs_file_ops;
  }
  
  d_add(child_dentry, inode);
  return NULL;
}

static int vtfs_iterate(struct file* filp, struct dir_context* ctx) {
  struct dentry* dentry;
  struct inode* inode;
  struct vtfs_dir* dir;
  struct vtfs_file* file;
  unsigned long pos;
  unsigned long file_index = 0;
  unsigned char ftype;
  
  if (!filp) {
    return 0;
  }
  
  dentry = filp->f_path.dentry;
  if (!dentry || !dentry->d_inode) {
    return 0;
  }
  
  inode = dentry->d_inode;
  dir = vtfs_get_dir(inode->i_sb, inode->i_ino);
  if (!dir) {
    return 0;
  }
  
  pos = ctx->pos;
  
  if (pos == 0) {
    if (!dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR)) {
      return 0;
    }
    ctx->pos = 1;
    pos = 1;
  }
  
  if (pos == 1) {
    ino_t parent_ino = dentry->d_parent->d_inode->i_ino;
    if (!dir_emit(ctx, "..", 2, parent_ino, DT_DIR)) {
      return 0;
    }
    ctx->pos = 2;
    pos = 2;
  }
  
  down_read(&dir->sem);
  
  unsigned long files_to_skip = 0;
  if (pos > 2) {
    files_to_skip = pos - 2;
  }
  
  list_for_each_entry(file, &dir->files, list) {
    if (file_index < files_to_skip) {
      file_index++;
      continue;
    }
    
    if (S_ISDIR(file->mode)) {
      ftype = DT_DIR;
    } else {
      ftype = DT_REG;
    }
    
    if (!dir_emit(ctx, file->name, strlen(file->name), file->ino, ftype)) {
      up_read(&dir->sem);
      return 0;
    }
    ctx->pos++;
    file_index++;
  }
  up_read(&dir->sem);
  
  return 0;
}

// Server integration functions

static int vtfs_server_create_file(struct vtfs_fs_info* info, ino_t parent_ino, const char* name, umode_t mode, ino_t* out_ino) {
  char response[256];
  char parent_ino_str[32], mode_str[32];
  int64_t ret;
  
  snprintf(parent_ino_str, sizeof(parent_ino_str), "%lu", parent_ino);
  snprintf(mode_str, sizeof(mode_str), "%o", mode & 0777);
  
  ret = vtfs_http_call(info->token, "create", response, sizeof(response), 3,
                       "parent_ino", parent_ino_str,
                       "name", name,
                       "mode", mode_str);
  
  if (ret < 0 || ret < 8) {
    return -EIO;
  }
  
  int64_t error_code = 0;
  for (int i = 0; i < 8; i++) {
    error_code = (error_code << 8) | ((unsigned char)response[i]);
  }
  
  if (error_code != 0) {
    return -EIO;
  }
  
  unsigned long ino;
  unsigned int file_mode;
  if (sscanf(response + 8, "%lu,%u", &ino, &file_mode) != 2) {
    return -EIO;
  }
  
  *out_ino = ino;
  return 0;
}

static int vtfs_server_write_file(struct vtfs_fs_info* info, ino_t ino, loff_t offset, const char* data, size_t len) {
  char response[64];
  char ino_str[32], offset_str[32];
  char* encoded_data;
  size_t encoded_len;
  int64_t ret;
  
  if (len == 0) {
    return 0;
  }
  
  encoded_len = ((len + 2) / 3) * 4 + 1;
  encoded_data = kmalloc(encoded_len, GFP_KERNEL);
  if (!encoded_data) {
    return -ENOMEM;
  }
  
  encode(data, encoded_data);
  
  snprintf(ino_str, sizeof(ino_str), "%lu", ino);
  snprintf(offset_str, sizeof(offset_str), "%lld", offset);
  
  ret = vtfs_http_call(info->token, "write", response, sizeof(response), 3,
                       "ino", ino_str,
                       "offset", offset_str,
                       "data", encoded_data);
  
  kfree(encoded_data);
  
  if (ret < 0) {
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    return -EIO;
  }
  
  return 0;
}

static int vtfs_server_read_file(struct vtfs_fs_info* info, ino_t ino, loff_t offset, size_t len, char* buffer, size_t* out_len) {
  char* response;
  char ino_str[32], offset_str[32], length_str[32];
  int64_t ret;
  size_t response_size;
  
  if (len == 0) {
    *out_len = 0;
    return 0;
  }
  
  response_size = 8 + len + 1024;
  response = kmalloc(response_size, GFP_KERNEL);
  if (!response) {
    return -ENOMEM;
  }
  
  snprintf(ino_str, sizeof(ino_str), "%lu", ino);
  snprintf(offset_str, sizeof(offset_str), "%lld", offset);
  snprintf(length_str, sizeof(length_str), "%zu", len);
  
  ret = vtfs_http_call(info->token, "read", response, response_size, 3,
                       "ino", ino_str,
                       "offset", offset_str,
                       "length", length_str);
  
  if (ret < 0) {
    kfree(response);
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    kfree(response);
    return -EIO;
  }
  
  size_t data_len = ret - 8;
  if (data_len > len) data_len = len;
  memcpy(buffer, response + 8, data_len);
  *out_len = data_len;
  
  kfree(response);
  return 0;
}

static int vtfs_server_delete_file(struct vtfs_fs_info* info, ino_t ino) {
  char response[64];
  char ino_str[32];
  int64_t ret;
  
  snprintf(ino_str, sizeof(ino_str), "%lu", ino);
  
  ret = vtfs_http_call(info->token, "delete", response, sizeof(response), 1,
                       "ino", ino_str);
  
  if (ret < 0) {
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    return -EIO;
  }
  
  return 0;
}

static int vtfs_server_mkdir(struct vtfs_fs_info* info, ino_t parent_ino, const char* name, umode_t mode, ino_t* out_ino) {
  char response[256];
  char parent_ino_str[32], mode_str[32];
  int64_t ret;
  
  snprintf(parent_ino_str, sizeof(parent_ino_str), "%lu", parent_ino);
  snprintf(mode_str, sizeof(mode_str), "%o", mode & 0777);
  
  ret = vtfs_http_call(info->token, "mkdir", response, sizeof(response), 3,
                       "parent_ino", parent_ino_str,
                       "name", name,
                       "mode", mode_str);
  
  if (ret < 0 || ret < 8) {
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    return -EIO;
  }
  
  unsigned long ino;
  unsigned int dir_mode;
  if (sscanf(response + 8, "%lu,%u", &ino, &dir_mode) != 2) {
    return -EIO;
  }
  
  *out_ino = ino;
  return 0;
}

static int vtfs_server_rmdir(struct vtfs_fs_info* info, ino_t ino) {
  char response[64];
  char ino_str[32];
  int64_t ret;
  
  snprintf(ino_str, sizeof(ino_str), "%lu", ino);
  
  ret = vtfs_http_call(info->token, "rmdir", response, sizeof(response), 1,
                       "ino", ino_str);
  
  if (ret < 0) {
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    return -EIO;
  }
  
  return 0;
}

static int vtfs_server_link(struct vtfs_fs_info* info, ino_t old_ino, ino_t parent_ino, const char* name, unsigned int* out_nlink) {
  char response[256];
  char old_ino_str[32], parent_ino_str[32];
  int64_t ret;
  
  snprintf(old_ino_str, sizeof(old_ino_str), "%lu", old_ino);
  snprintf(parent_ino_str, sizeof(parent_ino_str), "%lu", parent_ino);
  
  ret = vtfs_http_call(info->token, "link", response, sizeof(response), 3,
                       "old_ino", old_ino_str,
                       "parent_ino", parent_ino_str,
                       "name", name);
  
  if (ret < 0 || ret < 8) {
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    return -EIO;
  }
  
  unsigned long ino;
  unsigned int nlink;
  if (sscanf(response + 8, "%lu,%u", &ino, &nlink) != 2) {
    return -EIO;
  }
  
  *out_nlink = nlink;
  return 0;
}

static int vtfs_server_unlink(struct vtfs_fs_info* info, ino_t ino) {
  char response[64];
  char ino_str[32];
  int64_t ret;
  
  snprintf(ino_str, sizeof(ino_str), "%lu", ino);
  
  ret = vtfs_http_call(info->token, "unlink", response, sizeof(response), 1,
                       "ino", ino_str);
  
  if (ret < 0) {
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    return -EIO;
  }
  
  return 0;
}

static int vtfs_server_load_files(struct vtfs_fs_info* info, ino_t parent_ino) {
  char* response;
  char parent_ino_str[32];
  int64_t ret;
  size_t response_size = 8192;
  char* line;
  int files_loaded = 0;
  
  response = kmalloc(response_size, GFP_KERNEL);
  if (!response) {
    return -ENOMEM;
  }
  
  snprintf(parent_ino_str, sizeof(parent_ino_str), "%lu", parent_ino);
  
  ret = vtfs_http_call(info->token, "list", response, response_size, 1,
                       "parent_ino", parent_ino_str);
  
  if (ret < 0) {
    kfree(response);
    return -EIO;
  }
  
  int64_t error_code = *(int64_t*)response;
  error_code = be64_to_cpu(error_code);
  
  if (error_code != 0) {
    kfree(response);
    return -EIO;
  }
  
  line = response + 8;
  size_t data_len = ret - 8;
  
  while (*line && (line - (response + 8)) < data_len) {
    unsigned long ino;
    char name[VTFS_MAX_NAME];
    unsigned int mode;
    unsigned long data_size = 0;
    struct vtfs_dir* dir;
    struct vtfs_file* file;
    
    char* end_line = strchr(line, '\n');
    if (!end_line) break;
    *end_line = '\0';
    
    int parse_result = sscanf(line, "%lu,%255[^,],%u,%lu", &ino, name, &mode, &data_size);
    
    if (parse_result < 3) {
      parse_result = sscanf(line, "%lu,%255[^,],%u", &ino, name, &mode);
      data_size = 0;
    }
    
    if (parse_result >= 3) {
      if (mode >= 16384) {
        mode = S_IFDIR | (mode & 0777);
      } else {
        mode = S_IFREG | (mode & 0777);
      }
      dir = vtfs_get_dir(info->sb, parent_ino);
      if (!dir) {
        if (parent_ino == VTFS_ROOT_INO) {
          dir = &info->root_dir;
        } else {
          struct vtfs_file* parent_file = vtfs_find_file_by_ino(&info->root_dir, parent_ino);
          if (parent_file && parent_file->dir_data) {
            dir = parent_file->dir_data;
          } else {
            dir = &info->root_dir;
          }
        }
      }
      
      if (dir) {
        file = vtfs_create_file(dir, name, mode, ino);
        if (file) {
          file->data_size = data_size;
          if (S_ISDIR(mode)) {
            vtfs_server_load_files(info, ino);
          }
          files_loaded++;
        }
      }
    }
    
    line = end_line + 1;
  }
  
  kfree(response);
  return 0;
}

static int vtfs_create(
  struct mnt_idmap *idmap,
  struct inode *parent_inode, 
  struct dentry *child_dentry, 
  umode_t mode, 
  bool b
) {
  struct vtfs_fs_info* info;
  struct vtfs_dir* dir;
  struct vtfs_file* file;
  struct inode* inode;
  const char* name;
  
  if (!parent_inode || !child_dentry) {
    return -EINVAL;
  }
  
  info = parent_inode->i_sb->s_fs_info;
  if (!info) {
    return -ENOENT;
  }
  
  dir = vtfs_get_dir(parent_inode->i_sb, parent_inode->i_ino);
  if (!dir) {
    return -ENOENT;
  }
  
  name = child_dentry->d_name.name;
  
  umode_t file_mode = S_IFREG | (mode & 0777);
  if ((file_mode & 0777) == 0) {
    file_mode = S_IFREG | 0666;
  }
  
  ino_t new_ino = 0;
  if (info->use_server) {
    // Create file on server first
    int ret = vtfs_server_create_file(info, parent_inode->i_ino, name, file_mode, &new_ino);
    if (ret != 0) {
      return ret;
    }
    // Update next_ino to be higher than server ino
    if (new_ino >= info->next_ino) {
      info->next_ino = new_ino + 1;
    }
  } else {
    new_ino = info->next_ino++;
  }
  
  file = vtfs_create_file(dir, name, file_mode, new_ino);
  if (!file) {
    down_read(&dir->sem);
    if (vtfs_find_file(dir, name) != NULL) {
      up_read(&dir->sem);
      if (info->use_server) {
        vtfs_server_delete_file(info, new_ino);
      }
      return -EEXIST;
    }
    up_read(&dir->sem);
    if (info->use_server) {
      vtfs_server_delete_file(info, new_ino);
    }
    return -ENOMEM;
  }
  
  inode = vtfs_get_inode(parent_inode->i_sb, parent_inode, file->mode, file->ino);
  if (!inode) {
    vtfs_remove_file(dir, name);
    if (info->use_server) {
      vtfs_server_delete_file(info, new_ino);
    }
    return -ENOMEM;
  }
  
  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_file_ops;
  set_nlink(inode, 1);
  d_add(child_dentry, inode);
  
  return 0;
}

static struct vtfs_file* vtfs_get_file_by_inode(struct inode* inode) {
  struct vtfs_fs_info* info;
  struct vtfs_file* file;
  
  if (!inode || !inode->i_sb) {
    return NULL;
  }
  
  info = inode->i_sb->s_fs_info;
  if (!info) {
    return NULL;
  }
  
  file = vtfs_find_file_by_ino(&info->root_dir, inode->i_ino);
  
  return file;
}

static void vtfs_update_nlink_all(struct vtfs_dir* dir, ino_t ino, unsigned int nlink) {
  struct vtfs_file* file;
  
  if (!dir) return;
  
  down_write(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->ino == ino) {
      file->nlink = nlink;
    }
  }
  up_write(&dir->sem);
  
  down_read(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->dir_data) {
      vtfs_update_nlink_all(file->dir_data, ino, nlink);
    }
  }
  up_read(&dir->sem);
}

static void vtfs_update_data_all(struct vtfs_dir* dir, ino_t ino, char* old_data, char* new_data, size_t new_size) {
  struct vtfs_file* file;
  
  if (!dir) return;
  
  down_write(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->ino == ino && file->data == old_data) {
      file->data = new_data;
      file->data_size = new_size;
    }
  }
  up_write(&dir->sem);
  
  down_read(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->dir_data) {
      vtfs_update_data_all(file->dir_data, ino, old_data, new_data, new_size);
    }
  }
  up_read(&dir->sem);
}

static void vtfs_remove_all_by_ino(struct vtfs_dir* dir, ino_t ino) {
  struct vtfs_file* file;
  struct vtfs_file* tmp;
  
  if (!dir) return;
  
  down_write(&dir->sem);
  list_for_each_entry_safe(file, tmp, &dir->files, list) {
    if (file->ino == ino) {
      list_del(&file->list);
      kfree(file);
    }
  }
  up_write(&dir->sem);
  
  down_read(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->dir_data) {
      vtfs_remove_all_by_ino(file->dir_data, ino);
    }
  }
  up_read(&dir->sem);
}

static int vtfs_unlink(struct inode *parent_inode, struct dentry *child_dentry) {
  struct vtfs_dir* dir;
  struct vtfs_file* file;
  struct vtfs_file* main_file;
  struct inode* inode;
  const char* name;
  ino_t file_ino;
  unsigned int new_nlink;
  
  if (!parent_inode || !child_dentry) {
    return -EINVAL;
  }
  
  dir = vtfs_get_dir(parent_inode->i_sb, parent_inode->i_ino);
  if (!dir) {
    return -ENOENT;
  }
  
  name = child_dentry->d_name.name;
  inode = child_dentry->d_inode;
  
  if (!inode) {
    return -ENOENT;
  }
  
  file_ino = inode->i_ino;
  
  main_file = vtfs_get_file_by_inode(inode);
  if (!main_file) {
    return -ENOENT;
  }
  
  down_write(&dir->sem);
  file = vtfs_find_file(dir, name);
  if (!file || file->ino != file_ino) {
    up_write(&dir->sem);
    return -ENOENT;
  }
  
  new_nlink = main_file->nlink - 1;
  
  char* data_to_free = NULL;
  struct vtfs_dir* dir_data_to_free = NULL;
  bool should_free_data = (new_nlink == 0);
  
  if (should_free_data) {
    data_to_free = file->data;
    dir_data_to_free = file->dir_data;
  }
  
  list_del(&file->list);
  up_write(&dir->sem);
  
  struct vtfs_fs_info* info = parent_inode->i_sb->s_fs_info;
  if (info) {
    vtfs_update_nlink_all(&info->root_dir, file_ino, new_nlink);
    
    if (info->use_server) {
      int ret = vtfs_server_unlink(info, file_ino);
      if (ret != 0) {
        // Continue anyway
      }
    }
  }
  
  set_nlink(inode, new_nlink);
  
  kfree(file);
  
  if (should_free_data) {
    if (info) {
      vtfs_remove_all_by_ino(&info->root_dir, file_ino);
    }
    
    if (dir_data_to_free) {
      vtfs_cleanup_dir(dir_data_to_free);
      kfree(dir_data_to_free);
    }
    if (data_to_free) {
      kfree(data_to_free);
    }
  }
  
  return 0;
}

static int vtfs_mkdir(struct mnt_idmap *idmap, struct inode *parent_inode, struct dentry *child_dentry, umode_t mode) {
  struct vtfs_fs_info* info;
  struct vtfs_dir* dir;
  struct vtfs_file* file;
  struct inode* inode;
  const char* name;
  
  if (!parent_inode || !child_dentry) {
    return -EINVAL;
  }
  
  info = parent_inode->i_sb->s_fs_info;
  if (!info) {
    return -ENOENT;
  }
  
  dir = vtfs_get_dir(parent_inode->i_sb, parent_inode->i_ino);
  if (!dir) {
    return -ENOENT;
  }
  
  name = child_dentry->d_name.name;
  
  umode_t dir_mode = S_IFDIR | (mode & 0777);
  if ((dir_mode & 0777) == 0) {
    dir_mode = S_IFDIR | 0755;
  }
  
  ino_t new_ino = 0;
  if (info->use_server) {
    int ret = vtfs_server_mkdir(info, parent_inode->i_ino, name, dir_mode, &new_ino);
    if (ret != 0) {
      return ret;
    }
    if (new_ino >= info->next_ino) {
      info->next_ino = new_ino + 1;
    }
  } else {
    new_ino = info->next_ino++;
  }
  
  file = vtfs_create_file(dir, name, dir_mode, new_ino);
  if (!file) {
    down_read(&dir->sem);
    if (vtfs_find_file(dir, name) != NULL) {
      up_read(&dir->sem);
      if (info->use_server) {
        vtfs_server_rmdir(info, new_ino);
      }
      return -EEXIST;
    }
    up_read(&dir->sem);
    if (info->use_server) {
      vtfs_server_rmdir(info, new_ino);
    }
    return -ENOMEM;
  }
  
  inode = vtfs_get_inode(parent_inode->i_sb, parent_inode, file->mode, file->ino);
  if (!inode) {
    vtfs_remove_file(dir, name);
    if (info->use_server) {
      vtfs_server_rmdir(info, new_ino);
    }
    return -ENOMEM;
  }
  
  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_dir_ops;
  d_add(child_dentry, inode);
  
  return 0;
}

static int vtfs_rmdir(struct inode *parent_inode, struct dentry *child_dentry) {
  struct vtfs_dir* dir;
  struct vtfs_file* file;
  const char* name;
  
  if (!parent_inode || !child_dentry) {
    return -EINVAL;
  }
  
  dir = vtfs_get_dir(parent_inode->i_sb, parent_inode->i_ino);
  if (!dir) {
    return -ENOENT;
  }
  
  name = child_dentry->d_name.name;
  
  down_write(&dir->sem);
  file = vtfs_find_file(dir, name);
  if (!file) {
    up_write(&dir->sem);
    return -ENOENT;
  }
  
  if (!S_ISDIR(file->mode)) {
    up_write(&dir->sem);
    return -ENOTDIR;
  }
  
  if (!file->dir_data || !list_empty(&file->dir_data->files)) {
    up_write(&dir->sem);
    return -ENOTEMPTY;
  }
  
  ino_t file_ino = file->ino;
  list_del(&file->list);
  up_write(&dir->sem);
  
  struct vtfs_fs_info* info = parent_inode->i_sb->s_fs_info;
  if (info && info->use_server) {
    int ret = vtfs_server_rmdir(info, file_ino);
    if (ret != 0) {
      // Continue anyway
    }
  }
  
  if (file->dir_data) {
    kfree(file->dir_data);
  }
  kfree(file);
  
  return 0;
}

static int vtfs_link(struct dentry* old_dentry, struct inode* parent_dir, struct dentry* new_dentry) {
  struct vtfs_dir* dir;
  struct vtfs_file* file;
  struct vtfs_file* new_file;
  struct inode* inode;
  const char* name;
  
  if (!old_dentry || !parent_dir || !new_dentry) {
    return -EINVAL;
  }
  
  inode = old_dentry->d_inode;
  if (!inode) {
    return -ENOENT;
  }
  
  if (S_ISDIR(inode->i_mode)) {
    return -EPERM;
  }
  
  file = vtfs_get_file_by_inode(inode);
  if (!file) {
    return -ENOENT;
  }
  
  dir = vtfs_get_dir(parent_dir->i_sb, parent_dir->i_ino);
  if (!dir) {
    return -ENOENT;
  }
  
  name = new_dentry->d_name.name;
  
  down_write(&dir->sem);
  if (vtfs_find_file(dir, name) != NULL) {
    up_write(&dir->sem);
    return -EEXIST;
  }
  
  new_file = kmalloc(sizeof(struct vtfs_file), GFP_KERNEL);
  if (!new_file) {
    up_write(&dir->sem);
    return -ENOMEM;
  }
  
  INIT_LIST_HEAD(&new_file->list);
  new_file->ino = file->ino;
  new_file->mode = file->mode;
  strncpy(new_file->name, name, VTFS_MAX_NAME - 1);
  new_file->name[VTFS_MAX_NAME - 1] = '\0';
  new_file->dir_data = NULL;
  new_file->data = file->data;
  new_file->data_size = file->data_size;
  
  file->nlink++;
  new_file->nlink = file->nlink;
  set_nlink(inode, file->nlink);
  
  list_add_tail(&new_file->list, &dir->files);
  up_write(&dir->sem);
  
  struct vtfs_fs_info* info = parent_dir->i_sb->s_fs_info;
  if (info) {
    vtfs_update_nlink_all(&info->root_dir, file->ino, file->nlink);
    
    if (info->use_server) {
      unsigned int server_nlink;
      int ret = vtfs_server_link(info, file->ino, parent_dir->i_ino, name, &server_nlink);
      if (ret == 0) {
        // Update nlink from server response
        file->nlink = server_nlink;
        new_file->nlink = server_nlink;
        set_nlink(inode, server_nlink);
        vtfs_update_nlink_all(&info->root_dir, file->ino, server_nlink);
      } else {
        // Continue anyway
      }
    }
  }
  
  ihold(inode);
  d_instantiate(new_dentry, inode);
  
  return 0;
}

static ssize_t vtfs_read(struct file* filp, char __user* buffer, size_t len, loff_t* offset) {
  struct inode* inode;
  struct vtfs_file* file;
  ssize_t ret;
  size_t to_read;
  
  if (!filp || !filp->f_path.dentry) {
    return -EINVAL;
  }
  
  inode = filp->f_path.dentry->d_inode;
  if (!inode) {
    return -EINVAL;
  }
  
  file = vtfs_get_file_by_inode(inode);
  if (!file) {
    return 0;
  }
  
  struct vtfs_fs_info* info = inode->i_sb->s_fs_info;
  
  // Load data from server if needed
  if (info && info->use_server && (!file->data || *offset + len > file->data_size)) {
    size_t needed = *offset + len;
    if (needed > file->data_size || !file->data) {
      char* server_data = kmalloc(needed, GFP_KERNEL);
      if (server_data) {
        size_t read_len;
        int server_ret = vtfs_server_read_file(info, inode->i_ino, 0, needed, server_data, &read_len);
        if (server_ret == 0 && read_len > 0) {
          if (file->data) {
            kfree(file->data);
          }
          file->data = server_data;
          file->data_size = read_len;
        } else {
          kfree(server_data);
        }
      }
    }
  }
  
  if (!file->data) {
    return 0;
  }
  
  if (*offset >= file->data_size) {
    return 0;
  }
  
  to_read = len;
  if (*offset + to_read > file->data_size) {
    to_read = file->data_size - *offset;
  }
  
  if (to_read == 0) {
    return 0;
  }
  
  ret = copy_to_user(buffer, file->data + *offset, to_read);
  if (ret) {
    return -EFAULT;
  }
  
  *offset += to_read;
  return to_read;
}

static ssize_t vtfs_write(struct file* filp, const char __user* buffer, size_t len, loff_t* offset) {
  struct inode* inode;
  struct vtfs_file* file;
  char* new_data;
  size_t new_size;
  ssize_t ret;
  
  if (!filp || !filp->f_path.dentry) {
    return -EINVAL;
  }
  
  inode = filp->f_path.dentry->d_inode;
  if (!inode) {
    return -EINVAL;
  }
  
  file = vtfs_get_file_by_inode(inode);
  if (!file) {
    return -ENOENT;
  }
  
  if (filp->f_flags & O_APPEND) {
    *offset = file->data_size;
  }
  
  new_size = *offset + len;
  if (new_size > file->data_size || !file->data) {
    char* old_data = file->data;
    new_data = krealloc(file->data, new_size, GFP_KERNEL);
    if (!new_data) {
      return -ENOMEM;
    }
    if (file->data_size < *offset) {
      memset(new_data + file->data_size, 0, *offset - file->data_size);
    }
    
    struct vtfs_fs_info* info = inode->i_sb->s_fs_info;
    // For hard links: update all links if old_data exists
    if (info && old_data && old_data != new_data) {
      vtfs_update_data_all(&info->root_dir, inode->i_ino, old_data, new_data, new_size);
      file = vtfs_get_file_by_inode(inode);
      if (!file || !file->data) {
        return -ENOMEM;
      }
    } else {
      // New file or no hard links: just update this file
      file->data = new_data;
      file->data_size = new_size;
    }
  }
  
  if (!file->data) {
    return -ENOMEM;
  }
  
  // Copy data from user space first
  char* temp_buffer = kmalloc(len, GFP_KERNEL);
  if (!temp_buffer) {
    return -ENOMEM;
  }
  
  ret = copy_from_user(temp_buffer, buffer, len);
  if (ret) {
    kfree(temp_buffer);
    return -EFAULT;
  }
  
  // Copy to file->data
  memcpy(file->data + *offset, temp_buffer, len);
  
  // Send to server if in server mode
  struct vtfs_fs_info* info = inode->i_sb->s_fs_info;
  if (info && info->use_server) {
    int server_ret = vtfs_server_write_file(info, inode->i_ino, *offset, temp_buffer, len);
    if (server_ret != 0) {
      // Continue anyway - data is in memory
    }
  }
  
  kfree(temp_buffer);
  
  *offset += len;
  inode->i_size = file->data_size;
  
  return len;
}

static int vtfs_getattr(struct mnt_idmap* idmap, const struct path* path, struct kstat* stat, u32 request_mask, unsigned int flags) {
  struct inode* inode = d_inode(path->dentry);
  struct vtfs_file* file;
  
  if (!inode) {
    return -EINVAL;
  }
  
  generic_fillattr(idmap, request_mask, inode, stat);
  
  // Update stat from file structure
  file = vtfs_get_file_by_inode(inode);
  if (file) {
    stat->size = file->data_size;
    stat->nlink = file->nlink;
    stat->mode = file->mode;
  }
  
  return 0;
}

static int vtfs_setattr(struct mnt_idmap* idmap, struct dentry* dentry, struct iattr* attr) {
  struct inode* inode = dentry->d_inode;
  
  if (!inode) {
    return -EINVAL;
  }
  
  if (attr->ia_valid & ATTR_SIZE) {
    struct vtfs_file* file = vtfs_get_file_by_inode(inode);
    if (file) {
      char* old_data = file->data;
      char* new_data = NULL;
      
      if (attr->ia_size < file->data_size) {
        new_data = krealloc(file->data, attr->ia_size, GFP_KERNEL);
        if (new_data) {
          struct vtfs_fs_info* info = inode->i_sb->s_fs_info;
          if (info && old_data != new_data) {
            vtfs_update_data_all(&info->root_dir, inode->i_ino, old_data, new_data, attr->ia_size);
          } else {
            file->data = new_data;
            file->data_size = attr->ia_size;
          }
          inode->i_size = attr->ia_size;
        }
      } else if (attr->ia_size > file->data_size) {
        new_data = krealloc(file->data, attr->ia_size, GFP_KERNEL);
        if (new_data) {
          memset(new_data + file->data_size, 0, attr->ia_size - file->data_size);
          struct vtfs_fs_info* info = inode->i_sb->s_fs_info;
          if (info && old_data != new_data) {
            vtfs_update_data_all(&info->root_dir, inode->i_ino, old_data, new_data, attr->ia_size);
          } else {
            file->data = new_data;
            file->data_size = attr->ia_size;
          }
          inode->i_size = attr->ia_size;
        }
      }
    }
  }
  
  setattr_copy(idmap, inode, attr);
  return 0;
}

static struct inode_operations vtfs_inode_ops = {
  .lookup = vtfs_lookup,
  .getattr = vtfs_getattr,
  .create = vtfs_create,
  .unlink = vtfs_unlink,
  .mkdir = vtfs_mkdir,
  .rmdir = vtfs_rmdir,
  .link = vtfs_link,
  .setattr = vtfs_setattr,
};

static struct file_operations vtfs_dir_ops = {
  .owner = THIS_MODULE,
  .iterate_shared = vtfs_iterate,
};

static int vtfs_open(struct inode* inode, struct file* filp) {
  if (filp->f_flags & O_TRUNC) {
    struct vtfs_file* file = vtfs_get_file_by_inode(inode);
    if (file) {
      char* old_data = file->data;
      struct vtfs_fs_info* info = inode->i_sb->s_fs_info;
      
      if (old_data) {
        if (info) {
          vtfs_update_data_all(&info->root_dir, inode->i_ino, old_data, NULL, 0);
          file = vtfs_get_file_by_inode(inode);
          if (file) {
            file->data = NULL;
            file->data_size = 0;
          }
        } else {
          file->data = NULL;
          file->data_size = 0;
        }
        kfree(old_data);
      } else {
        // File has no data yet, just ensure it's NULL
        file->data = NULL;
        file->data_size = 0;
      }
      inode->i_size = 0;
    }
  }
  return 0;
}

static struct file_operations vtfs_file_ops = {
  .owner = THIS_MODULE,
  .open = vtfs_open,
  .read = vtfs_read,
  .write = vtfs_write,
};

static int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
  struct vtfs_fs_info* info;
  struct inode* inode;
  const char* token = (const char*)data;
  
  
  info = kmalloc(sizeof(struct vtfs_fs_info), GFP_KERNEL);
  if (!info) {
    return -ENOMEM;
  }
  
  INIT_LIST_HEAD(&info->root_dir.files);
  init_rwsem(&info->root_dir.sem);
  info->next_ino = 200;
  // Check if token is valid: not NULL, not empty string
  info->use_server = false;
  info->token = NULL;
  
  if (token && strlen(token) > 0 && strcmp(token, "") != 0) {
    info->token = kstrdup(token, GFP_KERNEL);
    if (info->token) {
      info->use_server = true;
    }
  }
  
  if (!info->use_server || info->token) {
  } else {
    kfree(info);
    return -ENOMEM;
  }
  
  sb->s_fs_info = info;
  info->sb = sb;
  
  inode = vtfs_get_inode(sb, NULL, S_IFDIR | 0777, VTFS_ROOT_INO);
  if (inode == NULL) {
    if (info->token) {
      kfree(info->token);
    }
    kfree(info);
    return -ENOMEM;
  }
  
  inode->i_op = &vtfs_inode_ops;
  inode->i_fop = &vtfs_dir_ops;
  
  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    if (info->token) {
      kfree(info->token);
    }
    kfree(info);
    return -ENOMEM;
  }
  
  // Load files from server if in server mode
  if (info->use_server) {
    int ret = vtfs_server_load_files(info, VTFS_ROOT_INO);
    if (ret != 0) {
      // Continue anyway - empty filesystem
    } else {
      // Count loaded files
      struct vtfs_file* file;
      int total_files = 0;
      down_read(&info->root_dir.sem);
      list_for_each_entry(file, &info->root_dir.files, list) {
        total_files++;
      }
      up_read(&info->root_dir.sem);
    }
  }
  
  return 0;
}

static struct dentry* vtfs_mount(
  struct file_system_type* fs_type,
  int flags,
  const char* dev_name,
  void* data
) {
  // Parse mount options to extract token
  // data contains "token=xxx" or NULL/empty for RAM mode
  char* token = NULL;
  
  
  if (data) {
    char* options = kstrdup((char*)data, GFP_KERNEL);
    if (options) {
      char* opt = options;
      char* key;
      char* value;
      
      // Parse "token=value"
      while ((key = strsep(&opt, ",")) != NULL) {
        value = strchr(key, '=');
        if (value) {
          *value = '\0';
          value++;
          if (strcmp(key, "token") == 0) {
            token = kstrdup(value, GFP_KERNEL);
            break;
          }
        }
      }
      kfree(options);
    }
  }
  
  if (!token) {
  }
  
  // Pass token as data to vtfs_fill_super
  struct dentry* ret = mount_nodev(fs_type, flags, token, vtfs_fill_super);
  
  // Free token after mount (vtfs_fill_super will kstrdup it if needed)
  if (token) {
    kfree(token);
  }
  
  if (ret == NULL) {
  } else {
  }
  return ret;
}

static void vtfs_kill_sb(struct super_block* sb) {
  struct vtfs_fs_info* info;
  
  if (!sb) {
    return;
  }
  
  info = sb->s_fs_info;
  if (info) {
    if (!info->use_server) {
      vtfs_cleanup_dir(&info->root_dir);
    }
    if (info->token) {
      kfree(info->token);
    }
    kfree(info);
    sb->s_fs_info = NULL;
  }
  
}

static int __init vtfs_init(void) {
  int ret = register_filesystem(&vtfs_fs_type);
  if (ret != 0) {
    return ret;
  }
  
  return 0;
}

static void __exit vtfs_exit(void) {
  unregister_filesystem(&vtfs_fs_type);
}

module_init(vtfs_init);
module_exit(vtfs_exit);
