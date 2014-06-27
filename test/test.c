#include <stdio.h>
#include <stdlib.h>

#include "lfsc.h"

#define ASSERT_MSG(_v, ...) if(!(_v)){ fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); exit(1); }

int main(int argc, char** argv)
{
	lfsc_ctx* ctx = lfsc_ctx_create();
	ASSERT_MSG(ctx, "could not allocate ctx");
	
	lfsc_status s = lfsc_ctx_connect(ctx, L"hello", -1);
	ASSERT_MSG(s == LFSC_SOK, "could not connect (%d)", s);

	s = lfsc_ctx_disconnect(ctx);
	ASSERT_MSG(s == LFSC_SOK, "could not disconnect (%d)", s);

	lfsc_ctx_destroy(ctx);
	return 0;
}
