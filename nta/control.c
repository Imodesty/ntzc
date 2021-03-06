/*
 * 	control.c
 *
 * 2010 Copyright (c) Ricardo Chen <ricardo.chen@semptianc.om>
 * All rights reserved.
 *
 * 2006 Copyright (c) Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#include <netinet/ip.h>

#include <linux/types.h>
#include <net/if.h>

#include "control.h"

#define dump_skb(s, p, l) \
do {\
    int i;\
    printf("\n%s %s packet: \n", __FUNCTION__, s);\
    for(i=0; i<l; i++) {\
		printf("%02x ", p[i]&0xff); \
        if((i+1)%8==0) {\
            printf( "\n");\
        }\
    } \
    printf( "\n"); \
}while(0)

//#define ZC_READ_WRITE	1

static struct zc_data *zcb[ZC_MAX_SNIFFERS];
static struct zc_ring_ctl *_zr[ZC_MAX_SNIFFERS];

static int zc_mmap(struct zc_user_control *ctl, struct zc_status *st)
{
	struct zc_data *e;
	unsigned int entry_num = st->entry_num;
	int i;

	printf("entry_num = %d\n", entry_num);
	for (i=0; i<entry_num; ++i) {
		struct zc_entry_status *ent = &st->entry[i];

		e = &ctl->node_entries[i];

		if (e->data.ptr || !ent->node_num)
			continue;

		e->data.ptr = mmap(NULL, PAGE_SIZE*(1<<ent->node_order)*ent->node_num, PROT_READ|PROT_WRITE, MAP_SHARED, ctl->fd, i*PAGE_SIZE);
		if (e->data.ptr == MAP_FAILED) {
			fprintf(stderr, "Failed to mmap: mmap: %2d.%2d: cpu: %d number: %u, order: %u, offset: %u, errno: %d - %s.\n", 
					i, st->entry_num, ctl->cpu, ent->node_num, ent->node_order, ctl->offset, errno, strerror(errno));
			e->data.ptr = NULL;
			return -1;
		}

		printf("mmap: %2d.%2d: cpu: %d, ptr: %p, number: %u, order: %u, offset: %u.\n", 
				i, st->entry_num, ctl->cpu, e->data.ptr, ent->node_num, ent->node_order, ctl->offset);
		ctl->offset += (1<<ent->node_order)*ent->node_num;
		e->entry = i;
		if(i==(entry_num-1) && ctl->cpu == 0) {
			int j;
			for(j=0; j<ZC_MAX_SNIFFERS; j++){
				zcb[j] = (struct zc_data*)(e->data.ptr+PAGE_SIZE+j*ctl->ring_num*sizeof(struct zc_data));
				_zr[j] =  (struct zc_ring_ctl *)(e->data.ptr + j*sizeof(struct zc_ring_ctl));
				printf("mmap ring zone: zcb[%d] %p _zr[%d] %p entry %p\n", j, zcb[j], j, _zr[j], e->data.ptr);
			}
		}
	}

	return 0;
}

static int zc_prepare(struct zc_user_control *ctl)//, unsigned int entry_num)
{
	int err;
	struct zc_status st;

	memset(&st, 0, sizeof(struct zc_status));
	st.entry_num = 0;
	err = ioctl(ctl->fd, ZC_STATUS, &st);
	if (err) {
		fprintf(stderr, "Failed to get status for CPU%d: %s [%d].\n", 
				ctl->cpu, strerror(errno), errno);
		return err;
	}

	err = zc_mmap(ctl,  &st);
	if (err)
		return err;
	return 0;
}

int zc_ctl_set_sniff(struct zc_user_control *zc, struct zc_sniff *zs)
{
	int err;

	err = ioctl(zc->fd, ZC_SET_SNIFF, zs);
	if(err) {
		perror("Failed to setup sniff mode: ");
		return err;
	}
	return 0;
}

int zc_ctl_enable_sniff(struct zc_user_control *zc, int enable, int id)
{
	int err;
	int command = enable? ZC_ENABLE_SNIFF: ZC_DISABLE_SNIFF;

	zc->sniffer_id = id;
	err = ioctl(zc->fd, command, &id);

	return err;

}

int zc_ctl_get_devid(struct zc_user_control *zc, char *dev_name)
{
	int err;
	struct zc_netdev zn;

	strncpy(zn.dev_name, dev_name, 8);

	err = ioctl(zc->fd, ZC_GET_NETDEV, &zn);
	if(err) {
		fprintf(stderr, "Failed to get dev %s index.\n", dev_name);
		return err;
	}
	return zn.index;
}

static char default_ctl_file[] = "/dev/zc";

struct zc_user_control *zc_ctl_init(int cpu_id, char *ctl_file)
{
	int err;
	struct zc_user_control  *ctl;

    if(!ctl_file) {
        ctl_file = default_ctl_file;
    }
	ctl = malloc(sizeof(struct zc_user_control));

	if (!ctl) {
		fprintf(stderr, "Failed to allocate control structures for CPU %d.\n", cpu_id);
		return NULL;
	}

	memset(ctl, 0, sizeof(struct zc_user_control));

	do {
		ctl->cpu = cpu_id; 
		ctl->fd = open(ctl_file, O_RDWR);
		if (!ctl->fd) {
			fprintf(stderr, "Failed to open control file %s: %s [%d].\n", ctl_file, strerror(errno), errno);
			return NULL;
		}

		err = ioctl(ctl->fd, ZC_SET_CPU, &ctl->cpu);
		if (err) {
			close(ctl->fd);
			fprintf(stderr, "Failed to setup CPU %d.\n", ctl->cpu);
			return NULL;
		}
		ctl->ring_num = SNIFFER_RING_NODES/ZC_MAX_SNIFFERS;
		printf("Init ring_num %d\n", ctl->ring_num);
		if (zc_prepare(ctl)){
			close(ctl->fd);
			fprintf(stderr, "Failed to prepare CPU%d.\n", ctl->cpu);
			return NULL;
		}
	}while(0);

	return ctl;
}

struct pollfd *_pfd;
int zc_ctl_prepare_polling(struct zc_user_control **zc_ctl, unsigned int nr_cpus)
{
    int i;
    if(_pfd) {
        fprintf(stderr, "polling already setupped\n");
        return -1;
    }
	_pfd = malloc(sizeof(struct pollfd) * nr_cpus);
	if (!_pfd) {
		fprintf(stderr, "Failed to allocate polling structures for %d cpus.\n", nr_cpus);
		return -2;
	}
	memset(_pfd, 0, sizeof(struct pollfd) * nr_cpus);

	for (i=0; i<nr_cpus; ++i) {
		_pfd[i].fd = zc_ctl[i]->fd;
		_pfd[i].events = POLLIN;
		_pfd[i].revents = 0;
	}
	return 0;
}

int zc_recv_loop(struct zc_user_control **zc_ctl, 
				 unsigned int nr_cpus,
				 char * param,
				 void (*zc_analyze)(void *ptr, int length, char *param))
{
		int poll_ready, i, j, pos;
		int err;
		unsigned int num, t_num=0;
		struct zc_ring ring;
		struct zc_data *zcr;

/* 
		poll_ready = poll(_pfd, nr_cpus, 1000);
		if (poll_ready == 0){
			return 0;
		}
		if (poll_ready < 0)
			return -1;
*/
		poll_ready = NTA_NR_CPUS;
		for (j=0; j<poll_ready; ++j) {
			int x = zc_ctl[j]->sniffer_id;
#if 0
			if ((!_pfd[j].revents & POLLIN))
				continue;
			
			_pfd[j].events = POLLIN;
			_pfd[j].revents = 0;
#endif
#ifdef ZC_READ_WRITE
			err = read(zc_ctl[j]->fd, &ring, sizeof(ring));
			if (err < 0) {
				fprintf(stderr, "Failed to read data from control file: %s [%d].\n", 
						strerror(errno), errno);
				return -2;
			}
#else
			ring.zc_pos = _zr[x]->zc_pos;
			ring.zc_used = _zr[x]->zc_dummy;
			if (ring.zc_pos >= ring.zc_used)
				err =  ring.zc_pos - ring.zc_used;
			else
				err =  zc_ctl[j]->ring_num - ring.zc_used + ring.zc_pos;
			if(0 && err) {
				printf("ctl %d num %d ctl->zc_used %d ctl->zc_dummy %d"
			   "ctl->zc_pos %d ctl->zc_prev_used %d\n",
			   x,
			   err , _zr[x]->zc_used , _zr[x]->zc_dummy, 
			   _zr[x]->zc_pos, _zr[x]->zc_prev_used);
			}
#endif
			//printf("read from zc_ctl[%d]->sniffer_id %d ring: num = %d used = %d pos = %d\n", 
			//	   j, zc_ctl[j]->sniffer_id, err, ring.zc_used, ring.zc_pos);
			zcr = zcb[x];
			num = err; 
			t_num += num;
			pos = ring.zc_used;
			for (i=0; i<num; ++i) {
				struct zc_data *z;
				char *ptr;
				struct zc_data *e;
				int t;

				t = pos;

				z = &zcr[pos++];

				if(pos == zc_ctl[j]->ring_num ) {
					pos = 0;
				}
				if (z->entry >= ZC_MAX_ENTRY_NUM  || z->cpu >= nr_cpus){
					printf("dump %4d.%4d: ptr: %p, size: %u, off: %u: entry: %u, cpu: %d\n", 
					i, num, z->data.ptr, z->r_size, z->off, z->entry, z->cpu);
					exit(0);
				}

				e = &zc_ctl[z->cpu]->node_entries[z->entry];

#if 0 
				printf("dump %4d.%4d: ptr: %p, size: %u, off: %u: entry: %u, cpu: %d\n", 
					i, num, z->data.ptr, z->r_size, z->off, z->entry, z->cpu);
#endif
				ptr = e->data.ptr + z->off;

				//dump_skb("1", ptr, 64);
				ptr += 66; // (NET_MBUF_PAD_ALLOC+NET_IP_ALIGN);
				//dump_skb("2", ptr, 64);

				//ptr = e->data.ptr;
				//zc_analyze_write(out_fd, ptr, z->size);
				(*zc_analyze)(ptr, z->r_size, param);
				_zr[x]->zc_dummy = pos;
			}
#ifdef ZC_READ_WRITE
			err = write(zc_ctl[j]->fd, &ring, sizeof(ring));
			if (err < 0) {
				fprintf(stderr, "Failed to write data to control file: %s [%d].\n", 
						strerror(errno), errno);
				return -3;
			} 
			if(err!=num) {
				printf("!!! read %d but write %d\n", num, err);
			}
#else
			//_zr[x]->zc_prev_used = ring.zc_pos;
#endif
		}
		return t_num;
}


inline char * zc_get(struct zc_user_control *ctl, struct zc_data **z)
{
	int ring_num, pos;
	unsigned int num;
	struct zc_ring ring;
	struct zc_data *zcr, *e;
	int x = ctl->sniffer_id;
	char *ptr;
	struct zc_data *_z;

	ring_num = ctl->ring_num;

	ring.zc_pos = _zr[x]->zc_pos;
	ring.zc_used = _zr[x]->zc_dummy;

	if (ring.zc_pos >= ring.zc_used)
		num =  ring.zc_pos - ring.zc_used;
	else
		num =  ring_num - ring.zc_used + ring.zc_pos;
	if(0 && num) {
		printf("ctl %d num %d ctl->zc_used %d ctl->zc_dummy %d"
			   "ctl->zc_pos %d ctl->zc_prev_used %d\n",
			   x,
			   num , _zr[x]->zc_used , _zr[x]->zc_dummy, 
			   _zr[x]->zc_pos, _zr[x]->zc_prev_used);
	}
	if(!num) 
		return NULL;

	zcr = zcb[x];
	_z = &zcr[ring.zc_used];
	
	if (_z->entry >= ZC_MAX_ENTRY_NUM ){// || z->cpu >= nr_cpus){
		printf("dump %d: ptr: %p, size: %u, off: %u: entry: %u, cpu: %d\n", 
			   num, _z->data.ptr, _z->r_size, _z->off, _z->entry, _z->cpu);
		exit(0);
	}
		
	e = &ctl->node_entries[_z->entry];

#if 0 
	printf("dump %4d.%4d: ptr: %p, size: %u, off: %u: entry: %u, cpu: %d\n", 
	i, num, z->data.ptr, z->r_size, z->off, z->entry, z->cpu);
#endif
	ptr = e->data.ptr + _z->off;
	
	//dump_skb("1", ptr, 64);
	ptr += 66; // (NET_MBUF_PAD_ALLOC+NET_IP_ALIGN);
	//dump_skb("2", ptr, 64);
	*z = _z;
	return ptr;
}

inline void zc_put(struct zc_user_control *ctl)
{
	int x = ctl->sniffer_id;
	unsigned int pos = _zr[x]->zc_dummy;
	if(++pos == ctl->ring_num) {
		pos = 0;
	}
	_zr[x]->zc_dummy = pos;

}


void * zc_alloc_buffer(struct zc_user_control *ctl,
                       struct zc_alloc_ctl *alloc_ctl)
{
    struct zc_data *z, *e;
    int err;
    void *ptr;
    
    err = ioctl(ctl->fd, ZC_ALLOC, alloc_ctl);
    if (err) {
        fprintf(stderr, "Failed to alloc from kernel: %s [%d].\n", strerror(errno), errno);
        return NULL;
    }
    z = &alloc_ctl->zc;
    //printf("cpu: %d, ptr: %p, size: %u [%u], reserve: %u, off: %u: entry: %u.\n", 
    //	z->cpu, z->data.ptr, z->size, size, res_len, z->off, z->entry);
    if (z->entry >= ZC_MAX_ENTRY_NUM){
        //|| z->cpu >= nr_cpus) {
        fprintf(stderr, "Wrong entry, exiting.\n");
        return NULL;
    }
    /*if (zc_prepare(ctl, z->entry))
        break;
    */
    e = &ctl->node_entries[z->entry];
    ptr = e->data.ptr + z->off;
    //printf("alloc: e->data.ptr %p z->data.ptr %p z->off %d\n", 
    //	   e->data.ptr, z->data.ptr, z->off);
    return ptr;
}

int zc_commit_buffer(struct zc_user_control *ctl, struct zc_alloc_ctl *alloc_ctl)
{
    int err;
	
    err = ioctl(ctl->fd, ZC_COMMIT, alloc_ctl);
    if (err) {
        fprintf(stderr, "Failed to commit buffer: %s [%d].\n", strerror(errno), errno);
        return err;
    }
    return 0;
}

void zc_ctl_shutdown(struct zc_user_control *zc)
{
    close(zc->fd);
    if(_pfd){
        free(_pfd);
        _pfd = NULL;
    }
}

