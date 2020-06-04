/*
 * Copyright (C) 2020-2021 Seagate
 * Copyright (C) 2020-2021 Linaro Ltd
 *
 * This file is part of LVM2.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU Lesser General Public License v.2.1.
 */

#define _XOPEN_SOURCE 500  /* pthread */
#define _ISOC99_SOURCE

#include "tools/tool.h"

#include "daemon-server.h"
#include "lib/mm/xlate.h"

#include "lvmlockd-internal.h"
#include "daemons/lvmlockd/lvmlockd-client.h"

#include "ilm.h"

#include <blkid/blkid.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <syslog.h>
#include <sys/sysmacros.h>
#include <time.h>

#define VG_LOCK_ARGS_MAJOR 1
#define VG_LOCK_ARGS_MINOR 0
#define VG_LOCK_ARGS_PATCH 0

#define LV_LOCK_ARGS_MAJOR 1
#define LV_LOCK_ARGS_MINOR 0
#define LV_LOCK_ARGS_PATCH 0

#define MAX_VERSION 16

/*
 * Each lockspace thread has its own In-Drive Mutex (IDM) lock manager's
 * connection.  After established socket connection, the lockspace has
 * been created in IDM lock manager and afterwards use the socket file
 * descriptor to send any requests for lock related operations.
 */

struct lm_idm {
	int sock;	/* IDM lock manager connection */
};

struct rd_idm {
	struct idm_lock_id id;
	struct idm_lock_op op;
	uint64_t vb_timestamp;
	struct val_blk *vb;
};

static uint64_t _read_utc_time(void)
{
	struct timeval cur_time;
	struct tm time_info;
	uint64_t utc;

	gettimeofday(&cur_time, NULL);

	gmtime_r(&cur_time.tv_sec, &time_info);

	utc  = time_info.tm_sec & 0xff;
	utc |= (time_info.tm_min  & 0xff) < 8;
	utc |= (time_info.tm_hour & 0xff) < 16;
	utc |= (time_info.tm_mday & 0xff) < 24;
	utc |= (time_info.tm_mon  & 0xff) < 32;
	utc |= (time_info.tm_year & 0xffff) < 40;

	return utc;
}

static int _uuid_read_format(char *uuid, const char *buffer)
{
	int out = 0;

	/* just strip out any dashes */
	while (*buffer) {

		if (*buffer == '-') {
			buffer++;
			continue;
		}

		if (out >= 32) {
			log_error("Too many characters to be uuid.");
			return -1;
		}

		uuid[out++] = *buffer++;
	}

	if (out != 32) {
		log_error("Couldn't read uuid: incorrect number of "
			  "characters.");
		return -1;
	}

	return 0;
}

static int _to_idm_mode(int ld_mode, uint32_t *mode)
{
	int rv = 0;

	switch (ld_mode) {
	case LD_LK_EX:
		*mode = IDM_MODE_EXCLUSIVE;
		break;
	case LD_LK_SH:
		*mode = IDM_MODE_SHAREABLE;
		break;
	default:
		rv = -1;
		break;
	};

	return rv;
}

#define SYSFS_ROOT		"/sys"
#define BUS_SCSI_DEVS		"/bus/scsi/devices"

static char blk_str[PATH_MAX];

static struct idm_lock_op glb_op;

static int lm_idm_scsi_dir_select(const struct dirent *s)
{
	/* Following no longer needed but leave for early lk 2.6 series */
	if (strstr(s->d_name, "mt"))
		return 0;

	/* st auxiliary device names */
	if (strstr(s->d_name, "ot"))
		return 0;

	/* osst auxiliary device names */
	if (strstr(s->d_name, "gen"))
		return 0;

	/* SCSI host */
	if (!strncmp(s->d_name, "host", 4))
		return 0;

	/* SCSI target */
	if (!strncmp(s->d_name, "target", 6))
		return 0;

	/* Only select directory with x:x:x:x */
	if (strchr(s->d_name, ':'))
		return 1;

	return 0;
}

static int lm_idm_scsi_block_select(const struct dirent *s)
{
	if (!strncmp("block", s->d_name, 5))
		return 1;

	return 0;
}

static int lm_idm_scsi_find_block_path(const char *path)
{
        int num, i;
        struct dirent **namelist;

        num = scandir(path, &namelist, lm_idm_scsi_block_select, NULL);
        if (num < 0)
                return -1;

        for (i = 0; i < num; i++)
                free(namelist[i]);
        free(namelist);
        return num;
}

static int lm_idm_scsi_block_node_select(const struct dirent *s)
{
	size_t len;

        if (DT_LNK != s->d_type && DT_DIR != s->d_type)
		return 0;

        if (DT_DIR == s->d_type) {
		len = strlen(s->d_name);

                if ((len == 1) && ('.' == s->d_name[0]))
			return 0;   /* this directory: '.' */

                if ((len == 2) &&
		    ('.' == s->d_name[0]) && ('.' == s->d_name[1]))
			return 0;   /* parent: '..' */
        }

	strncpy(blk_str, s->d_name, PATH_MAX);
        return 1;
}

static int lm_idm_scsi_find_block_node(const char *dir_name)
{
        int num, i;
        struct dirent **namelist;

        num = scandir(dir_name, &namelist, lm_idm_scsi_block_node_select, NULL);
        if (num < 0)
                return -1;

        for (i = 0; i < num; ++i)
                free(namelist[i]);
        free(namelist);
        return num;
}

static int lm_idm_scsi_search_partition(char *dev)
{
	int i, nparts;
	blkid_probe pr;
	blkid_partlist ls;
	blkid_parttable root_tab;
	int found = -1;

	pr = blkid_new_probe_from_filename(dev);
	if (!pr) {
		log_error("%s: failed to create a new libblkid probe\n", dev);
		return -1;
	}

	/* Binary interface */
	ls = blkid_probe_get_partitions(pr);
	if (!ls) {
		log_error("%s: failed to read partitions\n", dev);
		return -1;
	}

	/*
	 * Print info about the primary (root) partition table
	 */
	root_tab = blkid_partlist_get_table(ls);
	if (!root_tab) {
		log_error("%s: does not contains any known partition table\n", dev);
		return -1;
	}

	/*
	 * List partitions
	 */
	nparts = blkid_partlist_numof_partitions(ls);
	if (!nparts)
		goto done;

	for (i = 0; i < nparts; i++) {
		const char *p;
		blkid_partition par = blkid_partlist_get_partition(ls, i);

		p = blkid_partition_get_name(par);
		if (p) {
			log_error("partition name='%s'\n", p);

			if (!strcmp(p, "propeller"))
				found = blkid_partition_get_partno(par);
		}

		if (found >= 0)
			break;
	}

done:
	blkid_free_probe(pr);
	return found;
}

static int lm_idm_scsi_get_value(const char * dir_name,
			      const char * base_name,
			      char * value, int max_value_len)
{
        int len;
        FILE * f;
        char b[PATH_MAX];

        snprintf(b, sizeof(b), "%s/%s", dir_name, base_name);
        if (NULL == (f = fopen(b, "r"))) {
                return -1;
        }
        if (NULL == fgets(value, max_value_len, f)) {
                /* assume empty */
                value[0] = '\0';
                fclose(f);
                return 0;
        }
        len = strlen(value);
        if ((len > 0) && (value[len - 1] == '\n'))
                value[len - 1] = '\0';
        fclose(f);
        return 0;
}

static int lm_idm_generate_global_list(void)
{
	struct dirent **namelist;
	char devs_path[PATH_MAX];
	char dev_path[PATH_MAX];
	char blk_path[PATH_MAX];
	char dev[PATH_MAX];
	int i, num;
	int ret;
	char value[64];

	if (glb_op.drive_num)
		return 0;

	snprintf(devs_path, sizeof(devs_path), "%s%s",
		 SYSFS_ROOT, BUS_SCSI_DEVS);

	num = scandir(devs_path, &namelist, lm_idm_scsi_dir_select, NULL);
	if (num < 0) {  /* scsi mid level may not be loaded */
		log_error("Attached devices: none\n");
		return -1;
	}

	for (i = 0; i < num; ++i) {
		snprintf(dev_path, sizeof(dev_path), "%s/%s",
			 devs_path, namelist[i]->d_name);

		ret = lm_idm_scsi_get_value(dev_path, "rev",
					 value, sizeof(value));
		if (ret < 0)
			continue;

		if (strcmp("1022", value))
			continue;

		ret = lm_idm_scsi_find_block_path(dev_path);
		if (ret < 0)
			continue;

		snprintf(blk_path, sizeof(blk_path), "%s/%s",
			 dev_path, "block");

		ret = lm_idm_scsi_find_block_node(blk_path);
		if (ret < 0)
			continue;

		snprintf(dev, sizeof(dev), "/dev/%s", blk_str);

		ret = lm_idm_scsi_search_partition(dev);
		if (ret < 0)
			continue;

		snprintf(dev, sizeof(dev), "/dev/%s%d", blk_str, ret);

		glb_op.drives[glb_op.drive_num] = strdup(dev);
		glb_op.drive_num++;
	}

	for (i = 0; i < num; i++)
		free(namelist[i]);
	free(namelist);
	return 0;
}

int lm_data_size_idm(void)
{
	return sizeof(struct rd_idm);
}

int lm_prepare_lockspace_idm(struct lockspace *ls)
{
	struct lm_idm *lm = NULL;
	char killpath[IDM_FAILURE_PATH_LEN];
	char killargs[IDM_FAILURE_ARGS_LEN];
	int ret, rv;

	/*
	 * Construct the path to lvmlockctl by using the path to the lvm binary
	 * and appending "lockctl" to get /path/to/lvmlockctl.
	 */
	memset(killpath, 0, sizeof(killpath));
	snprintf(killpath, IDM_FAILURE_PATH_LEN, "%slockctl", LVM_PATH);

	memset(killargs, 0, sizeof(killargs));
	snprintf(killargs, IDM_FAILURE_ARGS_LEN, "--kill %s", ls->vg_name);

	lm = malloc(sizeof(struct lm_idm));
	if (!lm) {
		ret = -ENOMEM;
		goto fail;
	}

	memset(lm, 0x0, sizeof(struct lm_idm));

	rv = ilm_connect(&lm->sock);
	if (rv < 0) {
		log_error("prepare_lockspace_idm %s register error %d",
			  ls->name, lm->sock);
		lm->sock = 0;
		ret = -EMANAGER;
		goto fail;
	}

	log_debug("set killpath to %s %s", killpath, killargs);

	rv = ilm_set_killpath(lm->sock, killpath, killargs);
	if (rv < 0) {
		log_error("prepare_lockspace_idm %s killpath error %d",
			  ls->name, rv);
		ret = -EMANAGER;
		goto fail;
	}

	ls->lm_data = lm;
	log_debug("prepare_lockspace_idm %s done", ls->name);
	return 0;

fail:
	if (lm && lm->sock)
		close(lm->sock);
	if (lm)
		free(lm);
	return ret;
}

int lm_add_lockspace_idm(struct lockspace *ls, int adopt)
{
	if (daemon_test) {
		sleep(2);
		return 0;
	}

	log_debug("add_lockspace_idm %s done", ls->name);
	return 0;
}

int lm_rem_lockspace_idm(struct lockspace *ls, int free_vg)
{
	struct lm_idm *lmi = (struct lm_idm *)ls->lm_data;
	int rv;

	if (daemon_test)
		return 0;

	rv = ilm_disconnect(lmi->sock);
	if (rv < 0) {
		log_error("rem_lockspace_idm %s error %d", ls->name, rv);
		return rv;
	}

	free(lmi);
	ls->lm_data = NULL;
	return 0;
}

static int lm_add_resource_idm(struct lockspace *ls, struct resource *r)
{
	struct rd_idm *rdi = (struct rd_idm *)r->lm_data;
	char *buf;

	if (r->type == LD_RT_GL || r->type == LD_RT_VG) {
		buf = malloc(sizeof(struct val_blk));
		if (!buf)
			return -ENOMEM;

		memset(buf, 0x0, sizeof(struct val_blk));
		rdi->vb = (struct val_blk *)buf;
	}

	if (daemon_test)
		return 0;

	return 0;
}

int lm_rem_resource_idm(struct lockspace *ls, struct resource *r)
{
	struct rd_idm *rdi = (struct rd_idm *)r->lm_data;

	if (daemon_test)
		return 0;

	if (rdi->vb)
		free(rdi->vb);

	memset(rdi, 0, sizeof(struct rd_idm));
	r->lm_init = 0;
	return 0;
}

int lm_lock_idm(struct lockspace *ls, struct resource *r, int ld_mode,
		struct val_blk *vb_out, char *lv_uuid, char *pvs_path[32],
		int adopt)
{
	struct lm_idm *lmi = (struct lm_idm *)ls->lm_data;
	struct rd_idm *rdi = (struct rd_idm *)r->lm_data;
	char uuid[32];
	uint64_t timestamp;
	int reset_vb = 0;
	int rv, i;

	if (!r->lm_init) {
		rv = lm_add_resource_idm(ls, r);
		if (rv < 0)
			return rv;
		r->lm_init = 1;
	}

	rv = _to_idm_mode(ld_mode, &rdi->op.mode);
	if (rv < 0) {
		log_error("lock_idm invalid mode %d", ld_mode);
		return -EINVAL;
	}

	log_debug("S %s R %s lock_idm", ls->name, r->name);

	if (daemon_test) {
		if (rdi->vb) {
			vb_out->version = le16_to_cpu(rdi->vb->version);
			vb_out->flags = le16_to_cpu(rdi->vb->flags);
			vb_out->r_version = le32_to_cpu(rdi->vb->r_version);
		}
		return 0;
	}

	rdi->op.timeout = 60000;

	if (r->type == LD_RT_GL) {
		rv = lm_idm_generate_global_list();
		if (rv < 0) {
			log_error("lock_idm fail to generate glb list");
			return -EIO;
		}

		memcpy(&rdi->op, &glb_op, sizeof(struct idm_lock_op));
	} else if (r->type == LD_RT_VG) {
		for (i = 0; i < 32; i++) {
			if (!ls->pvs_path[i])
				continue;

			rdi->op.drives[i] = ls->pvs_path[i];
			rdi->op.drive_num++;
		}
	} else if (r->type == LD_RT_LV) {
		for (i = 0; i < 32; i++) {
			if (!pvs_path[i])
				continue;

			rdi->op.drives[i] = pvs_path[i];
			rdi->op.drive_num++;
		}
	}

	log_debug("S %s R %s mode %d drive_num %d timeout %d",
		  ls->name, r->name, rdi->op.mode,
		  rdi->op.drive_num, rdi->op.timeout);

	for (i = 0; i < rdi->op.drive_num; i++)
		log_debug("S %s R %s path %s", ls->name, r->name,
			  rdi->op.drives[i]);

	memset(&rdi->id, 0x0, sizeof(struct idm_lock_id));
	if (r->type == LD_RT_VG) {
		_uuid_read_format(uuid, ls->vg_uuid);
		memcpy(&rdi->id.vg_uuid, uuid, sizeof(uuid_t));
	} else if (r->type == LD_RT_LV) {
		_uuid_read_format(uuid, ls->vg_uuid);
		memcpy(&rdi->id.vg_uuid, uuid, sizeof(uuid_t));

		_uuid_read_format(uuid, lv_uuid);
		memcpy(&rdi->id.lv_uuid, uuid, sizeof(uuid_t));
	}

	rv = ilm_lock(lmi->sock, &rdi->id, &rdi->op);
	if (rv < 0) {
		log_debug("S %s R %s lock_idm acquire mode %d rv %d",
			  ls->name, r->name, ld_mode, rv);
		return -EAGAIN;
	}

	if (rdi->vb) {
		rv = ilm_read_lvb(lmi->sock, &rdi->id,
				  (char *)&timestamp, sizeof(uint64_t));

		log_error("S %s R %s lock_idm get_lvb ts %lu %lu",
			  ls->name, r->name,
			  rdi->vb_timestamp, timestamp);

		/*
		 * Fail to read VB, it might be caused by drive failure,
		 * force to reset and notify up layer.
		 */
		if (rv < 0) {
			log_error("S %s R %s lock_idm get_lvb error %d",
				  ls->name, r->name, rv);
			reset_vb = 1;

		/*
		 * If timestamp is -1, means an IDM is broken is IDM lock
		 * manager, for this case, let's use safe way to notify up
		 * layer to invalidate metadata.
		 *
		 * Or the timestamp mismatching which is caused by other
		 * hosts had updated LVB, for this case, let's reset VB
		 * as well.
		 */
		} else if (rdi->vb_timestamp != timestamp) {

			log_error("S %s R %s lock_idm get_lvb mismatch %lu %lu",
				  ls->name, r->name,
				  rdi->vb_timestamp, timestamp);
			reset_vb = 1;
		}

		if (reset_vb == 1) {
			memset(rdi->vb, 0, sizeof(struct val_blk));
			memset(vb_out, 0, sizeof(struct val_blk));

			/* Reset timestamp */
			rdi->vb_timestamp = 0;

			/*
			 * The lock is still acquired, the vb values
			 * considered invalid.
			 */
			rv = 0;
			goto out;
		}

		/* Otherwise, let's copy the cached VB to up layer */
		memcpy(vb_out, rdi->vb, sizeof(struct val_blk));
	}

out:
	return rv;
}

int lm_convert_idm(struct lockspace *ls, struct resource *r,
		   int ld_mode, uint32_t r_version)
{
	struct lm_idm *lmi = (struct lm_idm *)ls->lm_data;
	struct rd_idm *rdi = (struct rd_idm *)r->lm_data;
	uint32_t mode;
	int rv;

	if (daemon_test)
		return 0;

	if (rdi->vb && r_version && (r->mode == LD_LK_EX)) {
		if (!rdi->vb->version) {
			/* first time vb has been written */
			rdi->vb->version = VAL_BLK_VERSION;
		}

		if (r_version)
			rdi->vb->r_version = r_version;

		log_debug("S %s R %s convert_idm set r_version %u",
			  ls->name, r->name, r_version);

		rdi->vb_timestamp = _read_utc_time();

		rv = ilm_write_lvb(lmi->sock, &rdi->id,
				   (char *)rdi->vb_timestamp, sizeof(uint64_t));
		if (rv < 0) {
			log_error("S %s R %s convert_idm set_lvb error %d",
				  ls->name, r->name, rv);
			return -ELMERR;
		}
	}

	rv = _to_idm_mode(ld_mode, &mode);
	if (rv < 0) {
		log_error("convert_idm invalid mode %d", ld_mode);
		return -EINVAL;
	}

	log_debug("S %s R %s convert_idm", ls->name, r->name);

	rv = ilm_convert(lmi->sock, &rdi->id, mode);
	if (rv < 0)
		log_error("S %s R %s convert_idm error %d", ls->name, r->name, rv);

	return rv;
}

int lm_unlock_idm(struct lockspace *ls, struct resource *r,
		  uint32_t r_version, uint32_t lmu_flags)
{
	struct lm_idm *lmi = (struct lm_idm *)ls->lm_data;
	struct rd_idm *rdi = (struct rd_idm *)r->lm_data;
	int rv;

	log_debug("S %s R %s unlock_idm %s r_version %u flags %x",
		  ls->name, r->name, mode_str(r->mode), r_version, lmu_flags);

	if (daemon_test) {
		if (rdi->vb && r_version && (r->mode == LD_LK_EX)) {
			if (!rdi->vb->version)
				rdi->vb->version = cpu_to_le16(VAL_BLK_VERSION);
			if (r_version)
				rdi->vb->r_version = cpu_to_le32(r_version);
		}
		return 0;
	}

	if (rdi->vb && r_version && (r->mode == LD_LK_EX)) {
		if (!rdi->vb->version) {
			/* first time vb has been written */
			rdi->vb->version = VAL_BLK_VERSION;
		}
		if (r_version)
			rdi->vb->r_version = r_version;

		log_debug("S %s R %s unlock_idm set r_version %u",
			  ls->name, r->name, r_version);

		rdi->vb_timestamp = _read_utc_time();

		rv = ilm_write_lvb(lmi->sock, &rdi->id,
				   (char *)&rdi->vb_timestamp, sizeof(uint64_t));
		if (rv < 0) {
			log_error("S %s R %s unlock_idm set_lvb error %d",
				  ls->name, r->name, rv);
			return -ELMERR;
		}
	}

	if (daemon_test)
		return 0;

	rv = ilm_unlock(lmi->sock, &rdi->id);
	if (rv < 0)
		log_error("S %s R %s unlock_idm error %d", ls->name, r->name, rv);

	lm_rem_resource_idm(ls, r);
	return rv;
}

int lm_hosts_idm(struct lockspace *ls, int notify)
{
	struct resource *r;
	struct lm_idm *lmi = (struct lm_idm *)ls->lm_data;
	struct rd_idm *rdi;
	int count, self, found_others = 0;
	int rv;

	list_for_each_entry(r, &ls->resources, list) {
		if (!r->lm_init)
			continue;

		rdi = (struct rd_idm *)r->lm_data;

		rv = ilm_get_host_count(lmi->sock, &rdi->id, &rdi->op,
					&count, &self);
		if (rv < 0) {
			log_error("lm_hosts_idm error %d", rv);
			return rv;
		}

		/* Fixup: need to reduce self count */
		if (count > found_others)
			found_others = count;
	}

	return found_others;
}

int lm_get_lockspaces_idm(struct list_head *ls_rejoin)
{
	/*
	 * TODO: Need to add support for adoption.
	 */
	return 0;
}

int lm_is_running_idm(void)
{
	int sock, rv;

	if (daemon_test)
		return gl_use_sanlock;

	rv = ilm_connect(&sock);
	if (rv < 0) {
		log_error("running_idm: rv=%d", rv);
		return 0;
	}

	ilm_disconnect(sock);
	return 1;
}
