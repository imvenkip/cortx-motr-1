#ifndef __C2_DUMMY_INIT_FINI_H
#define __C2_DUMMY_INIT_FINI_H

int c2_trace_init(void);
void c2_trace_fini(void);

int c2_memory_init(void);
void c2_memory_fini(void);

int c2_threads_init(void);
void c2_threads_fini(void);

int c2_db_init(void);
void c2_db_fini(void);

int c2_linux_stobs_init(void);
void c2_linux_stobs_fini(void);

int c2_ad_stobs_init(void);
void c2_ad_stobs_fini(void);

int sim_global_init(void);
void sim_global_fini(void);

int c2_reqhs_init(void);
void c2_reqhs_fini(void);

#endif /* __C2_DUMMY_INIT_FINI_H */
