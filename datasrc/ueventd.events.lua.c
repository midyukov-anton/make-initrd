#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

#include "ueventd.h"

static int
pusherror(lua_State *state, const char *info)
{
	lua_pushnil(state);
	if (info)
		lua_pushfstring(state, "%s: %s", info, strerror(errno));
	else
		lua_pushstring(state, strerror(errno));
	lua_pushinteger(state, errno);
	return 3;
}

static int
pushresult(lua_State *state, int res, const char *info)
{
	if (res == -1)
		return pusherror(state, info);
	lua_pushboolean(state, 1);
	return 1;
}

static int
change_dir(lua_State *state)
{
	const char *path = luaL_checkstring(state, 1);
	return pushresult(state, chdir(path), NULL);
}

static int
make_dir(lua_State *state)
{
	const char *path = luaL_checkstring(state, 1);
	return pushresult(state, mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH), NULL);
}

static int
remove_dir(lua_State *state)
{
	const char *path = luaL_checkstring(state, 1);
	return pushresult(state, rmdir(path), NULL);
}

static int
scan_dir(lua_State *state)
{
	const char *path = luaL_checkstring(state, 1);
	struct dirent **namelist;
	int i, n;

	n = scandir(path, &namelist, NULL, alphasort);
	if (n < 0)
		return pusherror(state, "scandir");

	lua_newtable(state);

	for (i = 0; i < n; i++) {
		lua_pushstring(state, namelist[i]->d_name);
		lua_rawseti(state, -2, i + 1);
		free(namelist[i]);
	}
	free(namelist);
	return 1;
}

static int
read_dir(lua_State *state)
{
	const char *path = luaL_checkstring(state, 1);
	int i = 1;
	DIR *d;
	struct dirent *ent;

	if (!(d = opendir(path)))
		return pusherror(state, "opendir");

	lua_newtable(state);

	while ((ent = readdir(d)) != NULL) {
		lua_pushstring(state, ent->d_name);
		lua_rawseti(state, -2, i++);
	}
	closedir(d);

	return 1;
}

static const struct luaL_Reg fslib[] = {
	{"chdir", change_dir},
	{"mkdir", make_dir},
	{"rmdir", remove_dir},
	{"readdir", read_dir},
	{"scandir", scan_dir},
	{NULL, NULL},
};

static int
luaopen_fs(lua_State *state)
{
	luaL_newlib(state, fslib);
	lua_pushvalue(state, -1);
	lua_setglobal(state, "fs");

	lua_pushliteral(state, "FileSystem is a library for filesystem");
	lua_setfield(state, -2, "_DESCRIPTION");
	lua_pushliteral(state, "FileSystem 1.0");
	lua_setfield(state, -2, "_VERSION");

        return 1;
}

int
lua_make_rule(struct rules *r, const char *path)
{
	dbg("loading (lua): %s", path);

	r->handler.lua_state = luaL_newstate();

	luaL_openlibs(r->handler.lua_state);
	luaopen_fs(r->handler.lua_state);

	if (luaL_loadfile(r->handler.lua_state, path) != LUA_OK) {
		err("unable to load: %s: %s", path, lua_tostring(r->handler.lua_state, -1));
		lua_close(r->handler.lua_state);
		return 0;
	}

	if (lua_pcall(r->handler.lua_state, 0, 0, 0) != LUA_OK) {
		err("lua failed: %s", lua_tostring(r->handler.lua_state, -1));
		lua_close(r->handler.lua_state);
		return 0;
	}

	return 1;
}

void
lua_free_rule(struct rules *r)
{
	lua_close(r->handler.lua_state);
	free(r);
}

void
lua_process_rule(const struct rules *r)
{
	lua_getglobal(r->handler.lua_state, "main");
	if (lua_pcall(r->handler.lua_state, 0, 0, 0) != LUA_OK)
		err("lua failed: %s", lua_tostring(r->handler.lua_state, -1));
}
