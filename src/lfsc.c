#include "lfsc.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define dprintf(...)

typedef enum {
	LFSC_CDISCONNECT, LFSC_COPEN, LFSC_CCLOSE, LFSC_CREAD, LFSC_CWRITE, LFSC_CSEEK, 
	LFSC_CGET_LENGTH, LFSC_CSET_LENGTH, LFSC_CFLUSH, LFSC_CGET_CAN_SEEK, LFSC_CGET_CAN_READ, 
	LFSC_CGET_CAN_WRITE
} lfsc_cmd;

static const uint32_t lfsc_magic = 0xaa55aa55;

#define LFSC_OPERATION_SUCCESSFUL 0x0
#define LFSC_OPERATION_FAILED     0x1

// macro assumes that there's a var called ret and a label called error in the current scope
// if the expression evaluates to false, ret is set to _status and the program jumps to error
#define LFSC_TRY(_exp, _status) if(!(_exp)){ ret = _status; goto error; }

struct lfsc_ctx
{
	HANDLE pipe;
	HANDLE mutex;
};

struct lfsc_file
{
	lfsc_ctx* ctx;
	uint64_t handle;
};

// makes sure a buffer pointing to an int of arbitrary size is little endian
static void lfsc_flip_int_copy(void* dst, const void* src, size_t size)
{
	uint32_t a = 1;
	if(((char*)&a)[0] == 0){
		// big endian, flip
		for(int i = 0; i < size; i++)
			((uint8_t*)dst)[i] = ((uint8_t*)src)[size - 1 - i];
	}

	else {
		// little endian, copy
		memcpy(dst, src, size);
	}
}

// reads an LE int of arbitrary size from the pipe
static bool lfsc_read_int(HANDLE pipe, void* out_v, int size)
{
	dprintf("  %s\n", __func__);

	BOOL success;
	DWORD br;

	uint8_t buffer[size];
	success = ReadFile(pipe, buffer, size, &br, NULL);
	lfsc_flip_int_copy(out_v, buffer, size);

	return success && br == size;
}

// writes an LE int of arbitrary size to the pipe
static bool lfsc_write_int(HANDLE pipe, const void* v, int size)
{
	dprintf("  %s\n", __func__);

	BOOL success;
	DWORD br;

	uint8_t buffer[size];
	lfsc_flip_int_copy(buffer, v, size);

	success = WriteFile(pipe, buffer, size, &br, NULL);

	return success && br == size;
}

static bool lfsc_write_string(HANDLE pipe, const wchar_t* str, int32_t byte_length)
{
	if(!lfsc_write_int(pipe, &byte_length, 4))
		return false;
	
	DWORD br = 0;
	BOOL success = WriteFile(pipe, str, byte_length, &br, NULL);

	return success && br == byte_length;
}

static lfsc_status lfsc_check_status(HANDLE* pipe)
{
	uint32_t magic;

	if(!lfsc_read_int(pipe, &magic, 4))
		return LFSC_SERR_READ_PIPE;

	if(magic != lfsc_magic)
		return LFSC_SERR_BAD_MAGIC;

	uint32_t status;
	if(!lfsc_read_int(pipe, &status, 4))
		return LFSC_SERR_READ_PIPE;

	if(status != LFSC_OPERATION_SUCCESSFUL)
		return LFSC_SERR_OPERATION_FAILED;

	return LFSC_SOK;
}

static bool lfsc_write_command(HANDLE pipe, uint64_t handle, lfsc_cmd cmd)
{
	dprintf("  %s %d\n", __func__, cmd);

	dprintf("  magic\n");
	if(!lfsc_write_int(pipe, &lfsc_magic, sizeof(lfsc_magic)))
		return false;

	dprintf("  cmd\n");
	if(!lfsc_write_int(pipe, &cmd, sizeof(cmd)))
		return false;
	
	dprintf("  handle\n");
	if(!lfsc_write_int(pipe, &handle, sizeof(handle)))
		return false;

	return true;
}

static lfsc_status lfsc_local_seek(HANDLE pipe, uint64_t handle, int64_t offset, int32_t whence, int64_t* out)
{
	lfsc_status ret = LFSC_SERR_UNKNOWN;

	LFSC_TRY( lfsc_write_command(pipe, handle, LFSC_CSEEK), LFSC_SERR_WRITE_PIPE );
	LFSC_TRY( lfsc_write_int(pipe, &offset, sizeof(offset)), LFSC_SERR_WRITE_PIPE );
	LFSC_TRY( lfsc_write_int(pipe, &whence, sizeof(whence)), LFSC_SERR_WRITE_PIPE );

	lfsc_status s = lfsc_check_status(pipe);
	LFSC_TRY( s == LFSC_SOK, s );

	LFSC_TRY( lfsc_read_int(pipe, out, sizeof(int64_t)), LFSC_SERR_READ_PIPE );

	return LFSC_SOK;

error:
	return ret;
}

lfsc_ctx* lfsc_ctx_create()
{
	dprintf("%s\n", __func__);
	lfsc_ctx* ret = calloc(1, sizeof(lfsc_ctx));
	ret->pipe = INVALID_HANDLE_VALUE;
	ret->mutex = CreateMutex(NULL, FALSE, NULL);
	return ret;
}

void lfsc_ctx_destroy(lfsc_ctx* ctx)
{
	dprintf("%s\n", __func__);
	lfsc_ctx_disconnect(ctx);
	free(ctx);
}

lfsc_status lfsc_ctx_connect(lfsc_ctx* ctx, const wchar_t* name, int ms_timeout)
{
	dprintf("%s\n", __func__);
	lfsc_status status = LFSC_SERR_UNKNOWN;

	WaitForSingleObject(ctx->mutex, INFINITE);

	if(!WaitNamedPipeW(name, ms_timeout)){
		status = LFSC_SERR_BUSY;
		//int err = GetLastError();
		//dprintf("%d\n", err);

		goto error;
	}

	ctx->pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE | FILE_SHARE_READ | FILE_SHARE_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if(ctx->pipe == INVALID_HANDLE_VALUE){
		status = LFSC_SERR_OPEN_PIPE;
		goto error;
	}

	ReleaseMutex(ctx->mutex);
	return LFSC_SOK; 

error:
	ReleaseMutex(ctx->mutex);
	return status;
}

lfsc_status lfsc_ctx_disconnect(lfsc_ctx* ctx)
{
	if(ctx->pipe != INVALID_HANDLE_VALUE)
		CloseHandle(ctx->pipe);
	
	ctx->pipe = INVALID_HANDLE_VALUE;

	return LFSC_SOK;
}

int lfsc_test(lfsc_ctx* ctx, wchar_t* out_str, int str_size)
{
	uint8_t buffer[256];

	DWORD read = 0;
	DWORD avail = 0;
	DWORD left = 0;

	BOOL r = PeekNamedPipe(ctx->pipe, NULL, 0, &read, &avail, &left);

	if(!r)
		return swprintf(out_str, str_size, L"could not peek");

	r = ReadFile(ctx->pipe, buffer, avail, &read, NULL);

	int at = 0;
	at += swprintf(out_str + at, str_size - at, L"ok: %d, read: %d, avail: %d, left: %d, buffer: ", r, read, avail, left);
	
	for(int i = 0; i < read; i++){
		at += swprintf(out_str + at, str_size - at, L"%02x ", buffer[i]);
	}
	
	for(int i = 0; i < read; i++){
		at += swprintf(out_str + at, str_size - at, L"%c", buffer[i] >= 32 && buffer[i] < 128 ? buffer[i] : '.');
	}

	return at;
}

lfsc_status lfsc_ctx_fopen(lfsc_ctx* ctx, lfsc_file** out_file, const wchar_t* name)
{
	dprintf("%s\n", __func__);
	lfsc_status ret = LFSC_SERR_UNKNOWN;
	WaitForSingleObject(ctx->mutex, INFINITE);

	// write magic
	LFSC_TRY( lfsc_write_int(ctx->pipe, &lfsc_magic, sizeof(lfsc_magic)), LFSC_SERR_WRITE_PIPE );

	// write open command
	lfsc_cmd cmd = LFSC_COPEN;
	LFSC_TRY( lfsc_write_int(ctx->pipe, &cmd, sizeof(cmd)), LFSC_SERR_WRITE_PIPE );

	// write name of file to open
	// note: wcslen(name) * sizeof(wchar_t) is correct with utf-16, because wcslen() returns the number of wchar_t:s, 
	//       not real world characters
	LFSC_TRY( lfsc_write_string(ctx->pipe, name, wcslen(name) * sizeof(wchar_t)), LFSC_SERR_WRITE_PIPE );

	// check returned status
	lfsc_status s = lfsc_check_status(ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, s);

	// read file handle
	uint64_t file_handle;
	LFSC_TRY( lfsc_read_int(ctx->pipe, &file_handle, sizeof(file_handle)), LFSC_SERR_READ_PIPE );

	// create a file
	*out_file = calloc(1, sizeof(lfsc_file));
	LFSC_TRY( *out_file, LFSC_SERR_ALLOC );
	
	(*out_file)->ctx = ctx;
	(*out_file)->handle = file_handle;

	ReleaseMutex(ctx->mutex);
	return LFSC_SOK; 

error:
	ReleaseMutex(ctx->mutex);
	return ret;
}

size_t lfsc_read(void *ptr, size_t size, lfsc_file* stream)
{
	dprintf("%s %p %d %p\n", __func__, ptr, size, stream);
	size_t ret = 0;

	WaitForSingleObject(stream->ctx->mutex, INFINITE);

	// write handle
	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CREAD), 0 );

	// write size of read
	uint32_t size32 = size;
	dprintf("  requested size: %d\n", size32);
	LFSC_TRY( lfsc_write_int(stream->ctx->pipe, &size32, sizeof(size32)), 0 ); 

	// check status
	lfsc_status s = lfsc_check_status(stream->ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, 0 );
	
	// read actual size of read
	uint32_t recv_size = 0;
	LFSC_TRY( lfsc_read_int(stream->ctx->pipe, &recv_size, sizeof(recv_size)), 0 );
	dprintf("  actual size: %u\n", recv_size);

	// read data
	DWORD br = 0;
	if(recv_size > 0){
		dprintf("  readfile\n");
		LFSC_TRY( ReadFile(stream->ctx->pipe, ptr, recv_size, &br, NULL) , 0 );
		dprintf("  done\n");
	}

	ReleaseMutex(stream->ctx->mutex);

	// return bytes read
	dprintf("  received: %d bytes\n", br);
	return br;

error:
	ReleaseMutex(stream->ctx->mutex);
	dprintf("  error reading: %d\n", ret);
	return ret;
}

size_t lfsc_fread(void *ptr, size_t size, size_t nmemb, lfsc_file* stream)
{
	dprintf("%s %p %d %d %p\n", __func__, ptr, size, nmemb, stream);

	long read = lfsc_read(ptr, size * nmemb, stream);

	if(read != size * nmemb)
		lfsc_fseek(stream, -(read - (read % nmemb)), SEEK_CUR);

	return read / nmemb;
}

size_t lfsc_write(const void *ptr, size_t size, lfsc_file* stream)
{
	dprintf("%s\n", __func__);
	size_t ret = 0;

	WaitForSingleObject(stream->ctx->mutex, INFINITE);

	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CWRITE), 0 );

	uint32_t size32 = size;
	LFSC_TRY( lfsc_write_int(stream->ctx->pipe, &size32, sizeof(size32)), 0 ); 

	DWORD br;
	LFSC_TRY( WriteFile(stream->ctx->pipe, ptr, size, &br, NULL), 0 );
	
	lfsc_status s = lfsc_check_status(stream->ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, 0 );

	ReleaseMutex(stream->ctx->mutex);

	// return number of members read
	return br;

error:
	ReleaseMutex(stream->ctx->mutex);
	return ret;
}

size_t lfsc_fwrite(const void *ptr, size_t size, size_t nmemb, lfsc_file* stream)
{
	if(lfsc_write(ptr, size * nmemb, stream) == size * nmemb)
		return nmemb;
	
	return 0;
}

int lfsc_fseek(lfsc_file* stream, long offset, int whence)
{
	dprintf("%s\n", __func__);
	WaitForSingleObject(stream->ctx->mutex, INFINITE);

	int64_t out;
	lfsc_status status = lfsc_local_seek(stream->ctx->pipe, stream->handle, offset, whence, &out);

	if(status != LFSC_SOK)
		out = -1;

	ReleaseMutex(stream->ctx->mutex);
	
	return out;
}

int lfsc_fflush(lfsc_file* stream)
{
	dprintf("%s\n", __func__);
	int ret = 0;
	WaitForSingleObject(stream->ctx->mutex, INFINITE);

	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CCLOSE), EOF );
	LFSC_TRY( lfsc_check_status(stream->ctx->pipe) == LFSC_SOK, EOF );
	
error:
	ReleaseMutex(stream->ctx->mutex);
	return ret;
}

int lfsc_fclose(lfsc_file* stream)
{
	dprintf("%s\n", __func__);
	int ret;

	WaitForSingleObject(stream->ctx->mutex, INFINITE);

	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CCLOSE), EOF );
	lfsc_status s = lfsc_check_status(stream->ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, EOF );
	
	ReleaseMutex(stream->ctx->mutex);
	return 0;

error:
	ReleaseMutex(stream->ctx->mutex);
	return ret;
}

size_t lfsc_get_length(lfsc_file* stream)
{
	dprintf("%s\n", __func__);
	uint64_t ret;

	WaitForSingleObject(stream->ctx->mutex, INFINITE);

	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CGET_LENGTH), 0 );
	lfsc_status s = lfsc_check_status(stream->ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, 0 );
	
	LFSC_TRY( lfsc_read_int(stream->ctx->pipe, &ret, sizeof(ret)), 0 );

error:
	ReleaseMutex(stream->ctx->mutex);
	return ret;
}

lfsc_status lfsc_get_flags(lfsc_file* stream, int* out_flags)
{
	dprintf("%s\n", __func__);
	lfsc_status ret = LFSC_SOK;

	WaitForSingleObject(stream->ctx->mutex, INFINITE);
	
	uint8_t can_seek = 0, can_read = 0, can_write = 0;

	lfsc_status s;

	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CGET_CAN_SEEK), LFSC_SERR_WRITE_PIPE );
	s = lfsc_check_status(stream->ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, 0 );

	LFSC_TRY( lfsc_read_int(stream->ctx->pipe, &can_seek, 1), LFSC_SERR_READ_PIPE );
	
	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CGET_CAN_WRITE), LFSC_SERR_WRITE_PIPE );
	s = lfsc_check_status(stream->ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, 0 );
	
	LFSC_TRY( lfsc_read_int(stream->ctx->pipe, &can_write, 1), LFSC_SERR_READ_PIPE );
	
	LFSC_TRY( lfsc_write_command(stream->ctx->pipe, stream->handle, LFSC_CGET_CAN_READ), LFSC_SERR_WRITE_PIPE );
	s = lfsc_check_status(stream->ctx->pipe);
	LFSC_TRY( s == LFSC_SOK, 0 );
	
	LFSC_TRY( lfsc_read_int(stream->ctx->pipe, &can_read, 1), LFSC_SERR_READ_PIPE );

	if(can_seek)
		*out_flags |= LFSC_CAN_SEEK;
	
	if(can_read)
		*out_flags |= LFSC_CAN_READ;
	
	if(can_write)
		*out_flags |= LFSC_CAN_WRITE;
	
error:
	ReleaseMutex(stream->ctx->mutex);
	return ret;
}
