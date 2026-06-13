#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include "satellites.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NastyaAkhmerova");
MODULE_DESCRIPTION("SatelliteFS - Mars orbital satellite filesystem");

#define SATELLITEFS_MAGIC 0x53415446
#define MAX_FILES 256
#define MAX_CONTENT 4096
#define README_INO 2
#define ROOT_INO   1

static const char *README_TEXT =
    "SatelliteFS - Filesystem for Mars Orbital Satellites\n"
    "=====================================================\n"
    "\n"
    "Models of artificial satellites orbiting Mars (planet).\n"
    "Mars is the fourth planet of the Solar System (~1.52 AU).\n"
    "Solar irradiance at Mars is ~43%% of Earth value.\n"
    "\n"
    "Supported models:\n"
    "  ARES-1   - Low Mars Orbit (~400 km)\n"
    "             Operator: IMSA\n"
    "  PHOBOS-2 - Elliptical Mars Orbit (200x8000 km)\n"
    "             Operator: MRO Consortium\n"
    "  DEIMOS-3 - Areostationary Orbit (17032 km)\n"
    "             Operator: DeepSpace Relay Inc.\n"
    "\n"
    "Usage:\n"
    "  echo 'ANGLE SUNLOAD TXPOWER' > MODELNAME\n"
    "  cat MODELNAME.state\n"
    "\n"
    "This file is read-only.\n";

struct sfs_file {
    char name[256];
    char content[MAX_CONTENT];
    int  content_len;
    int  is_readonly;
    int  in_use;
    unsigned long ino;
};

static struct sfs_file sfs_files[MAX_FILES];
static int sfs_file_count = 0;
static unsigned long next_ino = 10;

static int find_file_by_name(const char *name)
{
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (sfs_files[i].in_use &&
            strcmp(sfs_files[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int find_free_slot(void)
{
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (!sfs_files[i].in_use)
            return i;
    }
    return -1;
}

static const SatelliteModel *parse_filename(const char *name)
{
    int i;
    for (i = 0; i < MAX_MODELS; i++) {
        const char *model = SATELLITE_MODELS[i].model_name;
        size_t model_len  = strlen(model);

        if (strcmp(name, model) == 0)
            return &SATELLITE_MODELS[i];

        if (strncmp(name, model, model_len) == 0 &&
            name[model_len] == '-') {
            const char *suffix = name + model_len + 1;
            long num = 0;
            const char *p;
            if (*suffix == '\0') continue;
            for (p = suffix; *p; p++) {
                if (*p < '0' || *p > '9') goto next;
                num = num * 10 + (*p - '0');
                if (num > 2147483647L) goto next;
            }
            if (num >= 1)
                return &SATELLITE_MODELS[i];
        }
next:;
    }
    return NULL;
}

static int parse_content(const char *content,
                          long *angle, long *sunload_x100,
                          long *txpower_x10)
{
    /* Парсим строку "ANGLE SUNLOAD TXPOWER" вручную без sscanf */
    const char *p = content;
    long a = 0, s_int = 0, s_frac = 0, t_int = 0, t_frac = 0;
    int frac_digits;

    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') a = a*10 + (*p++ - '0');
    if (a < 0 || a > 360) return 0;

    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') s_int = s_int*10 + (*p++ - '0');
    if (*p == '.') {
        p++;
        frac_digits = 0;
        while (*p >= '0' && *p <= '9' && frac_digits < 2) {
            s_frac = s_frac*10 + (*p++ - '0');
            frac_digits++;
        }
        while (frac_digits < 2) { s_frac *= 10; frac_digits++; }
    }
    if (s_int > 1 || (s_int == 1 && s_frac > 0)) return 0;

    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') t_int = t_int*10 + (*p++ - '0');
    if (*p == '.') {
        p++;
        frac_digits = 0;
        while (*p >= '0' && *p <= '9' && frac_digits < 1) {
            t_frac = t_frac*10 + (*p++ - '0');
            frac_digits++;
        }
    }

    while (*p == ' ' || *p == '\n' || *p == '\r') p++;
    if (*p != '\0') return 0;

    *angle        = a;
    *sunload_x100 = s_int * 100 + s_frac;
    *txpower_x10  = t_int * 10  + t_frac;
    return 1;
}

static void build_state(const SatelliteModel *model,
                         long angle, long sunload_x100, long txpower_x10,
                         char *buf, int buf_size)
{
    SatelliteState state;
    calculate_state(angle, sunload_x100, txpower_x10, model, &state);

    snprintf(buf, buf_size,
        "=== Satellite State Report ===\n"
        "Model       : %s\n"
        "Parent body : %s (%s)\n"
        "Operator    : %s\n"
        "Orbit       : %s\n"
        "Launch year : %d\n"
        "------------------------------\n"
        "Input parameters:\n"
        "  Sun angle : %ld degrees\n"
        "  Sun load  : %ld.%02ld\n"
        "  TX power  : %ld.%ld W\n"
        "------------------------------\n"
        "Current state:\n"
        "  Battery   : %d / %d Wh\n"
        "  Generated : %d W\n"
        "  Temp      : %d C (allowed: %d..%d C)\n"
        "  Mode      : %s\n",
        model->model_name,
        model->parent_body, model->parent_body_type,
        model->operator,
        model->orbit_type,
        model->launch_year,
        angle,
        sunload_x100 / 100, sunload_x100 % 100,
        txpower_x10  / 10,  txpower_x10  % 10,
        (int)state.battery_level, (int)(model->battery_capacity / 1000),
        (int)state.generated_power,
        (int)state.temperature,
        (int)(model->temp_min / 1000), (int)(model->temp_max / 1000),
        state.mode
    );
}

static void update_state_file(int idx)
{
    char state_name[512];
    long angle, sunload_x100, txpower_x10;
    int si, valid;
    const SatelliteModel *model;

    snprintf(state_name, sizeof(state_name),
             "%s.state", sfs_files[idx].name);

    valid = parse_content(sfs_files[idx].content,
                          &angle, &sunload_x100, &txpower_x10);

    if (valid) {
        model = parse_filename(sfs_files[idx].name);
        if (!model) return;

        si = find_file_by_name(state_name);
        if (si == -1) {
            si = find_free_slot();
            if (si == -1) return;
            memset(&sfs_files[si], 0, sizeof(struct sfs_file));
            strncpy(sfs_files[si].name, state_name,
                    sizeof(sfs_files[si].name) - 1);
            sfs_files[si].in_use     = 1;
            sfs_files[si].is_readonly = 1;
            sfs_files[si].ino        = next_ino++;
            sfs_file_count++;
        }

        build_state(model, angle, sunload_x100, txpower_x10,
                    sfs_files[si].content, MAX_CONTENT);
        sfs_files[si].content_len = strlen(sfs_files[si].content);

    } else {
        si = find_file_by_name(state_name);
        if (si != -1) {
            memset(&sfs_files[si], 0, sizeof(struct sfs_file));
            sfs_file_count--;
        }
    }
}

/* ============================================================
 * ОПЕРАЦИИ С ФАЙЛАМИ
 * ============================================================ */

static ssize_t sfs_read(struct file *filp, char __user *buf,
                         size_t count, loff_t *ppos)
{
    struct inode *inode = filp->f_inode;
    int idx = -1, i;
    size_t available, to_copy;

    if (inode->i_ino == README_INO) {
        size_t len = strlen(README_TEXT);
        if (*ppos >= len) return 0;
        available = len - *ppos;
        to_copy   = (count < available) ? count : available;
        if (copy_to_user(buf, README_TEXT + *ppos, to_copy))
            return -EFAULT;
        *ppos += to_copy;
        return to_copy;
    }

    for (i = 0; i < MAX_FILES; i++) {
        if (sfs_files[i].in_use &&
            sfs_files[i].ino == inode->i_ino) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return -ENOENT;

    if (*ppos >= sfs_files[idx].content_len) return 0;
    available = sfs_files[idx].content_len - *ppos;
    to_copy   = (count < available) ? count : available;
    if (copy_to_user(buf, sfs_files[idx].content + *ppos, to_copy))
        return -EFAULT;
    *ppos += to_copy;
    return to_copy;
}

static ssize_t sfs_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos)
{
    struct inode *inode = filp->f_inode;
    int idx = -1, i;

    for (i = 0; i < MAX_FILES; i++) {
        if (sfs_files[i].in_use &&
            sfs_files[i].ino == inode->i_ino) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return -ENOENT;
    if (sfs_files[idx].is_readonly) return -EACCES;

    if (*ppos + count > MAX_CONTENT - 1)
        count = MAX_CONTENT - 1 - *ppos;

    if (copy_from_user(sfs_files[idx].content + *ppos, buf, count))
        return -EFAULT;

    *ppos += count;
    sfs_files[idx].content_len = *ppos;
    sfs_files[idx].content[*ppos] = '\0';

    update_state_file(idx);
    return count;
}

static const struct file_operations sfs_file_ops = {
    .read  = sfs_read,
    .write = sfs_write,
};

/* ============================================================
 * ОПЕРАЦИИ С ДИРЕКТОРИЕЙ И INODE
 * ============================================================ */

static int sfs_iterate(struct file *filp, struct dir_context *ctx)
{
    int i;
    loff_t pos = 0;

    if (ctx->pos == 0) {
        if (!dir_emit_dot(filp, ctx)) return 0;
        ctx->pos = 1;
    }
    if (ctx->pos == 1) {
        if (!dir_emit_dotdot(filp, ctx)) return 0;
        ctx->pos = 2;
    }
    if (ctx->pos == 2) {
        if (!dir_emit(ctx, "README", 6, README_INO, DT_REG))
            return 0;
        ctx->pos = 3;
    }

    pos = 3;
    for (i = 0; i < MAX_FILES; i++) {
        if (!sfs_files[i].in_use) continue;
        if (ctx->pos > pos) { pos++; continue; }
        if (!dir_emit(ctx, sfs_files[i].name,
                      strlen(sfs_files[i].name),
                      sfs_files[i].ino, DT_REG))
            return 0;
        ctx->pos++;
        pos++;
    }
    return 0;
}

static const struct file_operations sfs_dir_ops = {
    .iterate_shared = sfs_iterate,
};

static struct inode *sfs_make_inode(struct super_block *sb,
                                     unsigned long ino, umode_t mode)
{
    struct inode *inode = new_inode(sb);
    if (!inode) return NULL;
    inode->i_ino   = ino;
    inode->i_mode  = mode;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    return inode;
}

static struct dentry *sfs_lookup(struct inode *dir,
                                  struct dentry *dentry,
                                  unsigned int flags)
{
    const char *name = dentry->d_name.name;
    struct inode *inode = NULL;
    int idx;

    if (strcmp(name, "README") == 0) {
        inode = sfs_make_inode(dir->i_sb, README_INO,
                               S_IFREG | 0444);
        if (!inode) return ERR_PTR(-ENOMEM);
        inode->i_fop  = &sfs_file_ops;
        inode->i_size = strlen(README_TEXT);
    } else {
        idx = find_file_by_name(name);
        if (idx >= 0) {
            umode_t mode = sfs_files[idx].is_readonly
                           ? S_IFREG | 0444
                           : S_IFREG | 0644;
            inode = sfs_make_inode(dir->i_sb,
                                   sfs_files[idx].ino, mode);
            if (!inode) return ERR_PTR(-ENOMEM);
            inode->i_fop  = &sfs_file_ops;
            inode->i_size = sfs_files[idx].content_len;
        }
    }

    d_add(dentry, inode);
    return NULL;
}

static int sfs_create(struct user_namespace *mnt_userns,
                       struct inode *dir, struct dentry *dentry,
                       umode_t mode, bool excl)
{
    const char *name = dentry->d_name.name;
    struct inode *inode;
    int idx;

    if (strstr(name, ".state")) return -EACCES;
    if (!parse_filename(name))  return -EINVAL;
    if (find_file_by_name(name) >= 0) return -EEXIST;

    idx = find_free_slot();
    if (idx == -1) return -ENOSPC;

    memset(&sfs_files[idx], 0, sizeof(struct sfs_file));
    strncpy(sfs_files[idx].name, name,
            sizeof(sfs_files[idx].name) - 1);
    sfs_files[idx].in_use      = 1;
    sfs_files[idx].is_readonly = 0;
    sfs_files[idx].ino         = next_ino++;
    sfs_file_count++;

    inode = sfs_make_inode(dir->i_sb, sfs_files[idx].ino,
                            S_IFREG | 0644);
    if (!inode) return -ENOMEM;
    inode->i_fop = &sfs_file_ops;
    d_instantiate(dentry, inode);
    return 0;
}

static int sfs_unlink(struct inode *dir, struct dentry *dentry)
{
    const char *name = dentry->d_name.name;
    char state_name[512];
    int idx, si;

    idx = find_file_by_name(name);
    if (idx == -1) return -ENOENT;
    if (sfs_files[idx].is_readonly) return -EACCES;

    memset(&sfs_files[idx], 0, sizeof(struct sfs_file));
    sfs_file_count--;

    snprintf(state_name, sizeof(state_name), "%s.state", name);
    si = find_file_by_name(state_name);
    if (si != -1) {
        memset(&sfs_files[si], 0, sizeof(struct sfs_file));
        sfs_file_count--;
    }
    return 0;
}

static int sfs_setattr(struct user_namespace *mnt_userns,
                        struct dentry *dentry, struct iattr *attr)
{
    const char *name = dentry->d_name.name;
    if (strcmp(name, "README") == 0) return -EACCES;
    if (strstr(name, ".state"))      return -EACCES;
    return 0;
}

static const struct inode_operations sfs_dir_inode_ops = {
    .lookup  = sfs_lookup,
    .create  = sfs_create,
    .unlink  = sfs_unlink,
};

static const struct inode_operations sfs_file_inode_ops = {
    .setattr = sfs_setattr,
};

/* ============================================================
 * СУПЕРБЛОК И РЕГИСТРАЦИЯ
 * ============================================================ */

static int sfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct inode *root;
    struct dentry *root_dentry;

    sb->s_magic     = SATELLITEFS_MAGIC;
    sb->s_blocksize = PAGE_SIZE;

    root = sfs_make_inode(sb, ROOT_INO, S_IFDIR | 0755);
    if (!root) return -ENOMEM;
    root->i_op  = &sfs_dir_inode_ops;
    root->i_fop = &sfs_dir_ops;

    root_dentry = d_make_root(root);
    if (!root_dentry) return -ENOMEM;
    sb->s_root = root_dentry;
    return 0;
}

static struct dentry *sfs_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name,
                                 void *data)
{
    return mount_nodev(fs_type, flags, data, sfs_fill_super);
}

static struct file_system_type satellitefs_type = {
    .owner   = THIS_MODULE,
    .name    = "satellitefs",
    .mount   = sfs_mount,
    .kill_sb = kill_litter_super,
};

static int __init satellitefs_init(void)
{
    memset(sfs_files, 0, sizeof(sfs_files));
    sfs_file_count = 0;
    printk(KERN_INFO "SatelliteFS: Mars orbital satellite filesystem loaded\n");
    return register_filesystem(&satellitefs_type);
}

static void __exit satellitefs_exit(void)
{
    unregister_filesystem(&satellitefs_type);
    printk(KERN_INFO "SatelliteFS: unloaded\n");
}

module_init(satellitefs_init);
module_exit(satellitefs_exit);
