#ifndef __COLIBRI_H
#define __COLIBRI_H

/* Structures, macros, etc. */

struct c2_io_req;
struct c2_fop;
struct c2_container;
struct c2_device;
struct c2_layout;
struct c2_foff;
struct c2_coff;
struct c2_doff;

//struct c2_node_id {
//};
struct c2_dev_id {

};


typedef struct c2_foff c2_foff_t;
typedef struct c2_coff c2_coff_t;
typedef struct c2_doff c2_doff_t;

struct c2_checksum {
	unsigned char csum[4];
};

#endif /* __COLIBRI_H */
