/**
 * memcache client consistent hash operations
 * adopted and modified from php-memcache which is
 * written by Antony Dovgal <tony@daylessday.org> and Mikael Johansson <mikael AT synd DOT info>                  |
 *
 * mcch might stand for Memcached Client Conistent Hash
 * or MemCache Consisten Hash, whichever you like
 *
 * @author microwish@gmail.com
 */
#include "lmcchlib.h"

#define DEFAULT_TIMEOUT_MS 1000

#define MC_STATUS_FAILED 0
#define MC_STATUS_DISCONNECTED 1
#define MC_STATUS_UNKNOWN 2
#define MC_STATUS_CONNECTED 3

#if 0
static struct timeval _convert_ms_to_ts(long msecs)
{
  struct timeval tv;
	long secs = 0;

	secs = msecs / 1000;
	tv.tv_sec = secs;
	tv.tv_usec = ((msecs - (secs * 1000)) * 1000) % 1000000;

	return tv;
}
#endif

static void mc_pool_init_hash(mc_pool_t *poolp)
{
	mc_hash_function hash;

	poolp->hash = &mc_consistent_hash;

	hash = &mc_hash_crc32;

	poolp->hash_state = poolp->hash->create_state(hash);
}

static char mc_pool_reg_key;

static mc_pool_t *get_mc_pool(lua_State *L, const char *machineroom, size_t room_len)
{
	mc_pool_t *poolp = NULL;

	//retrieve the main table for pools of memcache servers
	lua_pushlightuserdata(L, &mc_pool_reg_key);
	lua_rawget(L, LUA_REGISTRYINDEX);

	if (lua_type(L, -1) != LUA_TTABLE) {//not exists yet
		lua_pop(L, 1);
		lua_pushlightuserdata(L, &mc_pool_reg_key);
		//create the main table for pools of memcache servers at Lua register
		//{ machineroom1 = pool1_of_servers, machineroom2 = pool2_of_servers }
		//pool*_of_servers is of type mc_pool_t
		lua_newtable(L);

		//create a pool of servers for this machine room
		poolp = lua_newuserdata(L, sizeof(mc_pool_t));
		poolp->nservers = 0;
		mc_pool_init_hash(poolp);

		lua_setfield(L, -2, machineroom);

		lua_rawset(L, LUA_REGISTRYINDEX);
	} else {
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			if (!strncmp(lua_tostring(L, -2), machineroom, room_len)) {
				poolp = (mc_pool_t *)lua_touserdata(L, -1);
				lua_pop(L, 2);
				break;
			}
			lua_pop(L, 1);
		}
		//not found
		if (!poolp) {
			poolp = lua_newuserdata(L, sizeof(mc_pool_t));
			poolp->nservers = 0;
			mc_pool_init_hash(poolp);

			lua_setfield(L, -2, machineroom);
		}
		lua_pop(L, 1);
	}

	return poolp;
}

//XXX: reallocation or linked list
static int mc_pool_add(mc_pool_t *poolp, mc_t *mcp, unsigned int weight)
{
	if (poolp->nservers) {
		poolp->servers = realloc(poolp->servers, sizeof(mc_t *) * (poolp->nservers + 1));
	} else {
		poolp->servers = malloc(sizeof(mc_t *));
	}
	if (!poolp->servers) return 0;

	poolp->servers[poolp->nservers++] = mcp;

	poolp->hash->add_server(poolp->hash_state, mcp, weight);

	return 1;
}

static mc_t *mc_server_new(const char *host, size_t host_len, unsigned short port, int timeout_ms)
{
	mc_t *mcp = calloc(1, sizeof(mc_t));
	if (!mcp) return NULL;

#if defined(POSIX_C_SOURCE) && POSIX_C_SOURCE >= 200809L \
	|| defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 700
	if (!(mcp->host = strndup(host, host_len))) {
		free(mcp);
		return NULL;
	}
#else
	if (!(mcp->host = malloc(host_len + 1))) {
		free(mcp);
		return NULL;
	}
	memcpy(mcp->host, host, host_len);
	mcp->host[host_len] = 0;
#endif

	mcp->port = port;
	mcp->timeout_ms = timeout_ms;
	//mcp->status = MC_STATUS_DISCONNECTED;

	return mcp;
}

/**
 * {
 *  machineroom1 = {
 * 		{ host = "", port = 11211, weight = 1, timeout_ms = 1000 },
 * 		{ host = "", port = 11211, weight = 1, timeout_ms = 1000 }
 * 	},
 * 	machineroom2 = {
 * 		{ host = "", port = 11211, weight = 1, timeout_ms = 1000 },
 * 		{ host = "", port = 11211, weight = 1, timeout_ms = 1000 }
 * 	}
 * }
 */
static int add_servers(lua_State *L)
{
	if (lua_gettop(L) != 1 || lua_type(L, 1) != LUA_TTABLE) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "invalid argument");
		return 2;
	}

	//travers the outer-most table, whose string keys standing for machine rooms
	lua_pushnil(L);
	while (lua_next(L, 1)) {
		if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TTABLE) {
			lua_pushboolean(L, 0);
			lua_pushliteral(L, "invalid argument");
			return 2;
		}

		size_t room_len;
		const char *machineroom = lua_tolstring(L, -2, &room_len);

		mc_pool_t *poolp = get_mc_pool(L, machineroom, room_len);
		mc_t *mcp;
		const char *host;
		size_t host_len;
		unsigned short port;
		int weight, timeout_ms;

		//traverse the secondary inner-most table
		//with auto-incremented number keys starting from 1
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			//table key at (L, -2) should be of type number
			if (!lua_istable(L, -1)) {
				lua_pop(L, 1);
				continue;
			}

			host = NULL;
			port = 0;
			weight = -1;

			//traverse the inner-most table, with string keys
			lua_pushnil(L);
			while (lua_next(L, -2)) {
				if (lua_type(L, -2) != LUA_TSTRING) {
					lua_pop(L, 1);
					continue;
				}
				const char *k = lua_tostring(L, -2);
				if (!strncmp(k, "host", sizeof("host") - 1)) {
					if (!(host = lua_tolstring(L, -1, &host_len))) {
						lua_pushboolean(L, 0);
						lua_pushliteral(L, "invalid server host");
						return 2;
					}
				} else if (!strncmp(k, "port", sizeof("port") -1 )) {
					if (!(port = (unsigned short)lua_tointeger(L, -1))) {
						lua_pushboolean(L, 0);
						lua_pushliteral(L, "invalid server port");
						return 2;
					}
				} else if (!strncmp(k, "weight", sizeof("weight") - 1)) {
					if ((weight = (int)lua_tointeger(L, -1)) < 0) {
						lua_pushboolean(L, 0);
						lua_pushliteral(L, "invalid server weight");
						return 2;
					}
				} else if (!strncmp(k, "timeout_ms", sizeof("timeout_ms") - 1)) {
					if ((timeout_ms = (int)lua_tointeger(L, -1)) < 0) {
						lua_pushboolean(L, 0);
						lua_pushliteral(L, "invalid server timeout");
						return 2;
					}
				}
				lua_pop(L, 1);
			}//end -- the inner-most table

			//XXX
			if (!(mcp = mc_server_new(host, host_len, port, timeout_ms))) {
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "failed to make server %s:%d", host, port);
				return 2;
			}

			//XXX
			if (!mc_pool_add(poolp, mcp, weight)) {
				free(mcp);
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "failed to add server %s:%d", host, port);
				return 2;
			}

			lua_pop(L, 1);
		}//end -- the secondary inner-most table

		lua_pop(L, 1);
	}//end -- the outer-most table

	lua_pushboolean(L, 1);
	return 1;
}

static int pick_server(lua_State *L)
{
	if (lua_gettop(L) != 2) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "two arguments required");
		return 2;
	}
	if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "invalid arguments");
		return 2;
	}

	const char *key, *machineroom;
	size_t key_len, room_len;

	key = lua_tolstring(L, 1, &key_len);
	if (!key || key_len == 0) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "arg#1 invalid");
		return 2;
	}

	machineroom = lua_tolstring(L, 2, &room_len);
	if (!machineroom || room_len == 0) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "arg#2 invalid");
		return 2;
	}

	mc_pool_t *poolp = get_mc_pool(L, machineroom, room_len);

	mc_t *mcp = poolp->hash->find_server(poolp->hash_state, key, key_len);
	if (!mcp) {
		lua_pushnil(L);
		lua_pushliteral(L, "Failed to pick a server");
		return 2;
	}

	lua_pushstring(L, mcp->host);
	lua_pushinteger(L, mcp->port);
	lua_pushinteger(L, mcp->timeout_ms);
	return 3;
}

//TODO
//static int mc_pool_close();
//static void mc_pool_free();

static const luaL_Reg mcch_funcs[] = {
	{ "add_servers", add_servers },
	{ "pick_server", pick_server },
	{ NULL, NULL }
};

static int setreadonly(lua_State *L)
{
	return luaL_error(L, "Must not update the read-only");
}

#define LUA_MCCHLIBNAME "bd_mcch"

LUALIB_API int luaopen_bd_mcch(lua_State *L)
{
	//main table
	lua_createtable(L, 1, 0);

	//metatable for main table
	lua_createtable(L, 0, 2);

	luaL_setfuncs(L, mcch_funcs, 0);
	lua_pushvalue(L, -1);
	lua_setglobal(L, LUA_MCCHLIBNAME);

	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, setreadonly);
	lua_setfield(L, -2, "__newindex");

	lua_setmetatable(L, -2);

	return 1;
}
