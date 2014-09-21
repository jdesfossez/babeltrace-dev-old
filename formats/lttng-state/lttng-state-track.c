#include <libmemcached/memcached.h>
#include <glib.h>
#include <stdio.h>
#include "lttng-state.h"
#include "lttng-state-track.h"

#define LIST_DELIM "|"
#define HOSTNAMES_KEY "hostnames"
#define SESSIONS_SUFFIX "_sessions"

static
int mc_set(struct lttng_state_ctx *ctx, char *key, char *value)
{
	memcached_return rc;
	int ret;

	rc = memcached_set(ctx->memc, key, strlen(key), value, strlen(value),
			(time_t)0, (uint32_t)0);
	if (rc != MEMCACHED_SUCCESS) {
		fprintf(stderr,"Couldn't store key: %s\n",
				memcached_strerror(ctx->memc, rc));
		ret = -1;
		goto end;
	}
	ret = 0;

end:
	return ret;
}

/*
 * Allocates a new array, must be freed by caller.
 */
static
char **str_to_array(char *str, int *array_len)
{
	char **res = NULL;
	char *p = strtok(str, LIST_DELIM);
	int len = 0;

	/* split string and append tokens to 'res' */
	while (p) {
		++len;
		res = realloc(res, len * sizeof(char*));

		if (res == NULL)
			return NULL;
		res[len-1] = p;
		p = strtok (NULL, LIST_DELIM);
	}

	/* realloc one extra element for the last NULL */
	res = realloc(res, (len + 1) * sizeof(char*));
	res[len] = 0;

	*array_len = len;
	return res;
}

static
int mc_append(struct lttng_state_ctx *ctx, char *key, char *value, int uniq)
{
	memcached_return rc;
	int ret;
	char *mc_ret;
	uint32_t flags;
	size_t value_len;
	char *new_value, *str_ret;

	mc_ret = memcached_get(ctx->memc, key, strlen(key), &value_len, &flags, NULL);
	if (!mc_ret)
		return mc_set(ctx, key, value);

	if (uniq) {
		str_ret = strstr(mc_ret, value);
		if (str_ret) {
			ret = 0;
			goto end;
		}
	}

	new_value = malloc((strlen(value) + 2) * sizeof(char));
	if (!new_value) {
		ret = -1;
		goto end;
	}
	ret = snprintf(new_value, strlen(value) + 2, LIST_DELIM "%s", value);
	if (ret < 0) {
		ret = -1;
		goto end;
	}
	rc = memcached_append(ctx->memc, key, strlen(key), new_value,
			strlen(new_value), (time_t)0, (uint32_t)0);
	free(new_value);
	if (rc != MEMCACHED_SUCCESS) {
		fprintf(stderr,"Couldn't store key: %s\n",
				memcached_strerror(ctx->memc, rc));
		ret = -1;
		goto end;
	}
	ret = 0;

end:
	return ret;
}

static
int add_session(struct lttng_state_ctx *ctx)
{
	int ret;
	char *key;
	int len;

	len = strlen(ctx->traced_hostname) + sizeof(SESSIONS_SUFFIX);

	key = malloc(len * sizeof(char));
	if (!key) {
		ret = -1;
		goto end;
	}
	ret = sprintf(key, "%s" SESSIONS_SUFFIX, ctx->traced_hostname);
	if (ret < 0)
		goto end_free;
	ret = mc_append(ctx, key, ctx->session_name, 1);
	if (ret < 0)
		goto end_free;

end_free:
	free(key);
end:
	return ret;
}

static
int connect_memcached(struct lttng_state_ctx *ctx)
{
	int ret;

	memcached_server_st *servers = NULL;
	memcached_return rc;
	ctx->memc = memcached_create(NULL);
	servers = memcached_server_list_append(servers, "localhost", 11211, &rc);
	rc = memcached_server_push(ctx->memc, servers);
	if (rc == MEMCACHED_SUCCESS) {
		fprintf(stderr,"Added server successfully\n");
	} else {
		fprintf(stderr,"Couldn't add server: %s\n",
				memcached_strerror(ctx->memc, rc));
		ret = -1;
		goto end;
	}
	ret = mc_append(ctx, HOSTNAMES_KEY, ctx->traced_hostname, 1);
	if (ret < 0)
		goto end;

	ret = add_session(ctx);
	if (ret < 0)
		goto end;

	return ret;

	ret = 0;
end:
	return ret;
}

int lttng_state_init(struct lttng_state_ctx *ctx)
{
	int ret;

	if (!ctx->memc) {
		ret = connect_memcached(ctx);
		if (ret < 0)
			goto end;
	}
	ret = 0;

end:
	return ret;
}

int lttng_state_process_event(struct lttng_state_ctx *ctx,
	struct bt_stream_pos *ppos, struct ctf_stream_definition *stream)
{
	return 0;
}
