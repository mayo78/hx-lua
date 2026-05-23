#ifndef STATIC_LINK
#define IMPLEMENT_API
#endif

#if defined(HX_WINDOWS) || defined(HX_MACOS) || defined(HX_LINUX)
#define NEKO_COMPATIBLE
#endif

#include <hx/CFFI.h>
#include <cmath>
#include <cstring>


#include "../lua/src/lua.hpp"

vkind kind_lua_vm;

#define invalidhandle() hx_failure("invalid handle");

// forward declarations
int haxe_to_lua(value v, lua_State *l);
value lua_value_to_haxe(lua_State *l, int lua_v);

#define BEGIN_TABLE_LOOP(l, v) lua_pushnil(l); \
	while (lua_next(l, v) != 0) {
#define END_TABLE_LOOP(l) lua_pop(l, 1); }

inline value lua_table_to_haxe(lua_State *l, int lua_v)
{
	value v;
	int field_count = 0;
	bool array = true;

	// count the number of key/value pairs and figure out if it's an array or object
	BEGIN_TABLE_LOOP(l, lua_v)
		// check for all number keys (array), otherwise it's an object
		if (lua_type(l, -2) != LUA_TNUMBER) array = false;

		field_count += 1;
	END_TABLE_LOOP(l)

	if (array)
	{
		v = alloc_array(field_count);
		value *arr = val_array_value(v);
		BEGIN_TABLE_LOOP(l, lua_v)
			int index = (int)(lua_tonumber(l, -2) - 1); // lua has 1 based indices instead of 0
			arr[index] = lua_value_to_haxe(l, lua_v+2);
		END_TABLE_LOOP(l)
	}
	else
	{
		v = alloc_empty_object();
		BEGIN_TABLE_LOOP(l, lua_v)
			// TODO: don't assume string keys
			const char *key = lua_tostring(l, -2);
			alloc_field(v, val_id(key), lua_value_to_haxe(l, lua_v+2));
		END_TABLE_LOOP(l)
	}

	return v;
}

value lua_value_to_haxe(lua_State *l, int lua_v)
{
	lua_Number n;
	value v;
	switch (lua_type(l, lua_v))
	{
		case LUA_TNIL:
			v = val_null;
			break;
		case LUA_TNUMBER:
			n = lua_tonumber(l, lua_v);
			// check if number is int or float
			v = (fmod(n, 1) == 0) ? alloc_int(n) : alloc_float(n);
			break;
		case LUA_TTABLE:
			v = lua_table_to_haxe(l, lua_v);
			break;
		case LUA_TSTRING:
			v = alloc_string(lua_tostring(l, lua_v));
			break;
		case LUA_TBOOLEAN:
			v = alloc_bool(lua_toboolean(l, lua_v));
			break;
		case LUA_TFUNCTION:
		case LUA_TUSERDATA:
		case LUA_TTHREAD:
		case LUA_TLIGHTUSERDATA:
			printf("return value not supported");
			break;
	}
	return v;
}

static int haxe_callback(lua_State *l)
{
	int num_args = lua_gettop(l);
	AutoGCRoot *root = (AutoGCRoot *)lua_topointer(l, lua_upvalueindex(1));
	int expected_args = lua_tonumber(l, lua_upvalueindex(2));
	// functions made with Reflect.makeVarArgs (aka every hscript function) have an argument count of -1
	if (expected_args >= 0 && num_args != expected_args)
	{
		printf("Expected %d arguments, received %d", expected_args, num_args);
	}
	else
	{
		value *args = new value[num_args];
		for (int i = 0; i < num_args; ++i)
		{
			args[i] = lua_value_to_haxe(l, i + 1);
		}
		value result = val_callN(root->get(), args, num_args);
		delete [] args;
		return haxe_to_lua(result, l);
	}
	return 0;
}

inline void haxe_array_to_lua(value v, lua_State *l)
{
	int size = val_array_size(v);
	value *arr = val_array_value(v);
	lua_createtable(l, size, 0);
	for (int i = 0; i < size; i++)
	{
		lua_pushnumber(l, i + 1); // lua index is 1 based instead of 0
		haxe_to_lua(arr[i], l);
		lua_settable(l, -3);
	}
}

void haxe_iter_object(value v, field f, void *state)
{
	lua_State *l = (lua_State *)state;
	const char *name = val_string(val_field_name(f));
	lua_pushstring(l, name);
	haxe_to_lua(v, l);
	lua_settable(l, -3);
}

void haxe_iter_global(value v, field f, void *state)
{
	lua_State *l = (lua_State *)state;
	const char *name = val_string(val_field_name(f));
	haxe_to_lua(v, l);
	lua_setglobal(l, name);
}

// convert haxe values to lua
int haxe_to_lua(value v, lua_State *l)
{
	switch (val_type(v))
	{
		case valtNull:
			lua_pushnil(l);
			break;
		case valtBool:
			lua_pushboolean(l, val_bool(v));
			break;
		case valtFloat:
			lua_pushnumber(l, val_float(v));
			break;
		case valtInt:
			lua_pushnumber(l, val_int(v));
			break;
		case valtString:
			lua_pushstring(l, val_string(v));
			break;
		case valtFunction:
			// TODO: figure out a way to delete/cleanup the AutoGCRoot pointers
			lua_pushlightuserdata(l, new AutoGCRoot(v));
			lua_pushnumber(l, val_fun_nargs(v));
			// using a closure instead of a function so we can add upvalues
			lua_pushcclosure(l, haxe_callback, 2);
			break;
		case valtArray:
			//haxe_array_to_lua(v, l);
			lua_pushnil(l);
			break;
		case valtAbstractBase: // should abstracts be handled??
			printf("abstracts not supported");
			return 0;
		case valtObject: // falls through
		case valtEnum: // falls through
		case valtClass:
			lua_newtable(l);
			val_iter_fields(v, haxe_iter_object, l);
			break;
	}
	return 1;
}

static lua_State *lua_from_handle(value inHandle)
{
	if (val_is_kind(inHandle, kind_lua_vm))
	{
		lua_State *l = (lua_State *)val_to_kind(inHandle, kind_lua_vm);
		return l;
	}
	return NULL;
}


static value luahx_haxe_to_lua(value inHandle, value v)
{
	
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(haxe_to_lua(v, l) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_haxe_to_lua, 2);

static value luahx_lua_to_haxe(value inHandle, value idx)
{
	
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return lua_value_to_haxe(l, val_int(idx));
	}
	invalidhandle();
	return val_null;
}
DEFINE_PRIM(luahx_lua_to_haxe, 2);

static void release_lua(value inHandle)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_close(l);
	}
}

static value lua_create()
{
	lua_State *l = luaL_newstate();
	value result = alloc_abstract(kind_lua_vm, l);
	val_gc(result, release_lua);
	return result;
}
DEFINE_PRIM(lua_create, 0);

static value lua_get_version()
{
	return alloc_string(LUA_VERSION);
}
DEFINE_PRIM(lua_get_version, 0);

static value lua_load_context(value inHandle, value inContext)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		// load context, if any
		if (!val_is_null(inContext) && val_is_object(inContext))
		{
			val_iter_fields(inContext, haxe_iter_global, l);
		}
	}
	return val_null;
}
DEFINE_PRIM(lua_load_context, 2);

static value lua_call_function(value inHandle, value inFunction, value inArgs)
{
	const char *func = val_get_string(inFunction);
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_getglobal(l, func);

		int numArgs = 1;
		if (val_is_array(inArgs))
		{
			numArgs = val_array_size(inArgs);
			value *args = val_array_value(inArgs);
			for (int i = 0; i < numArgs; i++)
			{
				haxe_to_lua(args[i], l);
			}
		}
		else
		{
			haxe_to_lua(inArgs, l);
		}

		if (lua_pcall(l, numArgs, 1, 0) == 0)
		{
			value v = lua_value_to_haxe(l, -1);
			lua_pop(l, 1);
			return v;
		}
	}
	invalidhandle();
	return val_null;
}
DEFINE_PRIM(lua_call_function, 3);

// lua functions wrapped with a handle thing :)

static value luahx_gettop(value inHandle)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(lua_gettop(l));
	}
	invalidhandle();
	return alloc_int(-1);
}
DEFINE_PRIM(luahx_gettop, 1);

static void luahx_openlibs(value inHandle)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		luaL_openlibs(l);
		return;
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_openlibs, 1);

static void luahx_call(value inHandle, value nargs, value nresults)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_call(l, val_int(nargs), val_int(nresults));
		return;
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_call, 3);

static value luahx_pcall(value inHandle, value nargs, value nresults, value errfunc)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(lua_pcall(l, val_int(nargs), val_int(nresults), val_int(errfunc)));
	}
	invalidhandle();
	return alloc_int(LUA_ERRERR);
}
DEFINE_PRIM(luahx_pcall, 4);

static value luahx_dofile(value inHandle, value inScript)
{
	value v;
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(luaL_dofile(l, val_string(inScript)));
	}
	invalidhandle();
	return alloc_int(LUA_ERRERR);
}
DEFINE_PRIM(luahx_dofile, 2);

static value luahx_dostring(value inHandle, value inScript)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(luaL_dostring(l, val_string(inScript)));
	}
	invalidhandle();
	return alloc_int(LUA_ERRERR);
}
DEFINE_PRIM(luahx_dostring, 2);

static void luahx_setglobal(value inHandle, value name)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_setglobal(l, val_string(name));
		return;
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_setglobal, 2);

static void luahx_getglobal(value inHandle, value name)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_getglobal(l, val_string(name));
		return;
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_getglobal, 2);

static void luahx_pop(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_pop(l, val_int(idx));
		return;
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_pop, 2);

static value luahx_isnumber(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isnumber(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isnumber, 2);

static value luahx_isstring(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isstring(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isstring, 2);

static value luahx_iscfunction(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_iscfunction(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_iscfunction, 2);

static value luahx_isuserdata(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isuserdata(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isuserdata, 2);

static value luahx_isfunction(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isfunction(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isfunction, 2);

static value luahx_istable(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_istable(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_istable, 2);

static value luahx_islightuserdata(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_islightuserdata(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_islightuserdata, 2);

static value luahx_isnil(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isnil(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isnil, 2);

static value luahx_isboolean(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isboolean(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isboolean, 2);

static value luahx_isthread(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isthread(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isthread, 2);

static value luahx_isnone(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isnone(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isnone, 2);

static value luahx_isnoneornil(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_isnoneornil(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_isnoneornil, 2);

static value luahx_type(value inHandle, value tp)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(lua_type(l, val_int(tp)));
	}
	invalidhandle();
	return alloc_int(LUA_TNONE);
}
DEFINE_PRIM(luahx_type, 2);

static value luahx_typename(value inHandle, value tp)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_string(lua_typename(l, val_int(tp)));
	}
	invalidhandle();
	return val_null;
}
DEFINE_PRIM(luahx_typename, 2);

static value luahx_tonumber(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_float(lua_tonumber(l, val_int(idx)));
	}
	invalidhandle();
	return alloc_float(0);
}
DEFINE_PRIM(luahx_tonumber, 2);

static value luahx_tointeger(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(lua_tointeger(l, val_int(idx)));
	}
	invalidhandle();
	return alloc_int(0);
}
DEFINE_PRIM(luahx_tointeger, 2);

static value luahx_toboolean(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_bool(lua_toboolean(l, val_int(idx)) != 0);
	}
	invalidhandle();
	return val_false;
}
DEFINE_PRIM(luahx_toboolean, 2);

static value luahx_tostring(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_string(lua_tostring(l, val_int(idx)));
	}
	invalidhandle();
	return val_null;
}
DEFINE_PRIM(luahx_tostring, 2);

static void luahx_createtable(value inHandle, value narray, value nrec)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_createtable(l, val_int(narray), val_int(nrec));
		return;	
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_createtable, 3);

static void luahx_settable(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_settable(l, val_int(idx));
		return;	
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_settable, 2);

static value luahx_next(value inHandle, value idx)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(lua_next(l, val_int(idx)));
	}
	invalidhandle();
	return alloc_int(0);
}
DEFINE_PRIM(luahx_next, 2);

static void luahx_rawgeti(value inHandle, value idx, value n)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		lua_rawgeti(l, val_int(idx), val_int(n));
		return;
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_rawgeti, 3);

// lual

static value luahx_ref(value inHandle, value t)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return alloc_int(luaL_ref(l, val_int(t)));
	}
	invalidhandle();
	return alloc_int(0);
}
DEFINE_PRIM(luahx_ref, 2);

static void luahx_unref(value inHandle, value t, value ref)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		luaL_unref(l, val_int(t), val_int(ref));
		return;
	}
	invalidhandle();
}
DEFINE_PRIM(luahx_unref, 3);

extern "C" void lua_main()
{
	// no neko no neko
	//kind_share(&kind_lua_vm, "lua::vm"); // Fix Neko init
}
DEFINE_ENTRY_POINT(lua_main);

extern "C" int lua_register_prims() { return 0; }
