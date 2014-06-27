#ifndef LFSC_H
#define LFSC_H

#include <wchar.h>

#define LFSC_WAIT_FOREVER -1

typedef struct lfsc_ctx lfsc_ctx;
typedef struct lfsc_file lfsc_file;

typedef enum {
	LFSC_SOK,
	LFSC_SERR_ALLOC,
	LFSC_SERR_BUSY,
	LFSC_SERR_OPEN_PIPE,
	LFSC_SERR_READ_PIPE,
	LFSC_SERR_WRITE_PIPE,
	LFSC_SERR_OPERATION_FAILED,
	LFSC_SERR_BAD_MAGIC,
	LFSC_SERR_UNKNOWN
} lfsc_status;

typedef enum {
	LFSC_CAN_SEEK = 1,
	LFSC_CAN_READ = 2,
	LFSC_CAN_WRITE = 4
} lfsc_flags;

typedef enum {
	LFSC_SEEK_SET = 0, 
	LFSC_SEEK_CUR = 1,  
	LFSC_SEEK_END = 2
} lfsc_whence;

lfsc_ctx* lfsc_ctx_create();
void lfsc_ctx_destroy(lfsc_ctx* ctx);

lfsc_status lfsc_ctx_connect(lfsc_ctx* ctx, const wchar_t* name, int ms_timeout);
lfsc_status lfsc_ctx_disconnect(lfsc_ctx* ctx);
lfsc_status lfsc_ctx_fopen(lfsc_ctx* ctx, lfsc_file** out_file, const wchar_t* name);

size_t lfsc_fread(void *ptr, size_t size, size_t nmemb, lfsc_file* stream);
size_t lfsc_fwrite(const void *ptr, size_t size, size_t nmemb, lfsc_file* stream);
int lfsc_fseek(lfsc_file* stream, long offset, int whence);
int lfsc_fflush(lfsc_file* stream);
int lfsc_fclose(lfsc_file* stream); 

int lfsc_test(lfsc_ctx* ctx, wchar_t* out_str, int str_size);

size_t lfsc_get_length(lfsc_file* stream);
lfsc_status lfsc_get_flags(lfsc_file* stream, int* out_flags);

#endif
