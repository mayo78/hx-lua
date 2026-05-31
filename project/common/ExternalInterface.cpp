#ifndef STATIC_LINK
#define IMPLEMENT_API
#endif

#if defined(HX_WINDOWS) || defined(HX_MACOS) || defined(HX_LINUX)
#define NEKO_COMPATIBLE
#endif

#include <hx/CFFI.h>
#include <cmath>
#include <cstring>
#include "Utils.h"

#include "../lua/src/lua.hpp"

#define PI 3.1415926535897932385
#define TO_RAD PI / 180
#define TO_DEG 180 / PI

using namespace utils;

vkind kind_lua_vm;

#define invalidhandle() hx_failure("invalid handle");

// forward declarations
int haxe_to_lua(value v, lua_State *l);
value lua_value_to_haxe(lua_State *l, int lua_v);

#define BEGIN_TABLE_LOOP(l, v) lua_pushnil(l); \
	while (lua_next(l, v) != 0) {
#define END_TABLE_LOOP(l) lua_pop(l, 1); }

static value haxe_trace;
static void setHaxeTrace(value t)
{
	haxe_trace = t;
}
DEFINE_PRIM(setHaxeTrace, 1);

#define trace(s) val_call1(haxe_trace, alloc_string(s));
#define push(arr, v) val_call1(val_field(arr, pushID), v);

// from FlxMath
inline double fastSin(double n)
{
	n *= 0.3183098862; // divide by pi to normalize

	// bound between -1 and 1
	if (n > 1)
	{
		const int a = ceil(n);
		n -= (a >> 1) << 1;
	}
	else if (n < -1)
	{
		const int a = ceil(-n);
		n += (a >> 1) << 1;
	}

	// this approx only works for -pi <= rads <= pi, but it's quite accurate in this region
	if (n > 0)
	{
		return n * (3.1 + n * (0.5 + n * (-7.2 + n * 3.6)));
	}
	else
	{
		return n * (3.1 - n * (0.5 + n * (7.2 + n * 3.6)));
	}
}

inline double fastCos(double n)
{
	return fastSin(n + 1.570796327); // sin and cos are the same, offset by pi/2
}

inline double fastTan(double n)
{
	return fastSin(n) / fastCos(n);
}

inline double lerp(double a, double b, double x)
{
	return a + x * (b - a);
}

inline double bound(double x, double a, double b)
{
	return max(min(x, b), a);
}

inline void rotate(double &x, double &y, double degrees)
{
	const double radians = degrees * TO_RAD;
	x = (x * fastCos(radians)) - (y * fastSin(radians));
	y = (x * fastSin(radians)) + (y * fastCos(radians));
}

inline double SCALE(double value, double start1, double stop1, double start2, double stop2)
{
	return start2 + (value - start1) * ((stop2 - start2) / (stop1 - start1));
}

// zxy
inline void rotate3(double &x, double &y, double &z, double xa, double ya, double za)
{
	rotate(x, y, za);
	//rotate(z, y, xa);
	//rotate(x, z, ya);
}

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

static double songPosition = .0;
static double songBeat = .0;

static void registerNoteCallbacks(value inHandle)
{
	lua_State *l = lua_from_handle(inHandle);
	if (l)
	{
		return;
	}
	invalidhandle();
}

static int cur_pn = -1;
static void setCurPN(value pn)
{
	cur_pn = val_int(pn);
}
DEFINE_PRIM(setCurPN, 1);

static value conductor_getTimeInBeats;
static void setGetTimeInBeats(value callback)
{
	conductor_getTimeInBeats = callback;
}
DEFINE_PRIM(setGetTimeInBeats, 1);


static lua_State *cur_l;
static void setCurLua(value inHandle)
{
	cur_l = lua_from_handle(inHandle);
}
DEFINE_PRIM(setCurLua, 1);

static double arrowSize;
static void setArrowSize(value size)
{
	arrowSize = val_float(size);
}
DEFINE_PRIM(setArrowSize, 1);

static int updateModLua(value ms, value beat, value elapsed)
{
	songPosition = val_float(ms);
	songBeat = val_float(beat);

	// push song positions
	if (cur_l)
	{
		lua_pushnumber(cur_l, songPosition);
		lua_setglobal(cur_l, "songPos");
		lua_pushnumber(cur_l, songBeat);
		lua_setglobal(cur_l, "beat");
	
		// call update
		lua_getglobal(cur_l, "update");
		lua_pushnumber(cur_l, val_float(elapsed));
		int result = lua_pcall(cur_l, 1, 0, 0);
		return result;
	}
	return -1;
}
DEFINE_PRIM(updateModLua, 3);

// 
// Update ids
//

static int strumTimeID = 0;
static int noteDataID = 0;
static int strumID = 0;
static int isSustainNoteID = 0;
static int nextNoteID = 0;
static int sustainLengthID = 0;
static int wasGoodHitID = 0;

static int widthID = 0;
static int heightID = 0;

// WE ALL HATE CODENAME ENGINE IT FUCKING SUCKS
static int strumRelativePosID = 0;
static int updateNotesPosXID = 0;
static int updateNotesPosYID = 0;
static int copyStrumAngleID = 0;

// points
static int offsetID = 0;
static int originID = 0;
static int scaleID = 0;
static int dragID = 0;

static int xID = 0;
static int yID = 0;
static int zID = 0;

static int angleID = 0;
static int angleChangedID = 0; // something we need to set for the angle to actually update

static int alphaMultiplierID = 0;
static int colorTransformID = 0;
static int redOffsetID = 0;
static int greenOffsetID = 0;
static int blueOffsetID = 0;
static int alphaID = 0;

static int idID = 0;


//
// Rendering ids
//
static int startTrianglesBatchID = 0;
static int graphicID = 0;
static int antialiasingID = 0;

static int verticesID = 0;
static int indicesID = 0;
static int uvtDataID = 0;
static int colorMultipliersID = 0;
static int colorOffsetsID = 0;
static int alphasID = 0;
static int shaderID = 0;

static int arrayID = 0;
static int lengthID = 0;
static int getLengthID = 0;
static int setLengthID = 0;
static int pushID = 0;
static int frameID = 0;
static int uvID = 0;
static int parentID = 0;
static int mapGetID = 0;
static int extraID = 0;
static int bodyID = 0;
static int capID = 0;
static int holdBodyFramesID = 0;
static int holdCapFramesID = 0;
static int visibleID = 0;
static int sourceSizeID = 0;

static int holdGraphicID = 0;
static int bodyFrameID = 0;
static int capFrameID = 0;

// MATRICES!
static int identityID = 0;
static int appendScaleID = 0;
static int appendRotationID = 0;
static int appendTranslationID = 0;
static int transformVectorID = 0;
static int setToID = 0;

static int rawDataID = 0;
static int projectID = 0;

static value getIDs()
{
	strumTimeID = val_id("strumTime");
	noteDataID = val_id("noteData");
	isSustainNoteID = val_id("isSustainNote");
	nextNoteID = val_id("nextNote");
	sustainLengthID = val_id("sustainLength");
	wasGoodHitID = val_id("wasGoodHit");

	widthID = val_id("width");
	heightID = val_id("height");

	strumRelativePosID = val_id("strumRelativePos");
	updateNotesPosXID = val_id("updateNotesPosX");
	updateNotesPosYID = val_id("updateNotesPosY");
	copyStrumAngleID = val_id("copyStrumAngle");

	offsetID = val_id("offset");
	originID = val_id("origin");
	scaleID = val_id("scale");
	dragID = val_id("drag");

	xID = val_id("x");
	yID = val_id("y");
	zID = val_id("z");
	
	angleID = val_id("angle");
	angleChangedID = val_id("_angleChanged");
	
	colorTransformID = val_id("colorTransform");
	alphaMultiplierID = val_id("alphaMultiplier");
	redOffsetID = val_id("redOffset");
	greenOffsetID = val_id("greenOffset");
	blueOffsetID = val_id("blueOffset");
	alphaID = val_id("alpha");

	idID = val_id("ID"); // wow stupid

	startTrianglesBatchID = val_id("startTrianglesBatch");

	graphicID = val_id("graphic");
	antialiasingID = val_id("antialiasing");

	verticesID = val_id("vertices");
	indicesID = val_id("indices");
	uvtDataID = val_id("uvtData");
	colorMultipliersID = val_id("colorMultipliers");
	colorOffsetsID = val_id("colorOffsets");
	alphasID = val_id("alphas");
	shaderID = val_id("shader");

	arrayID = val_id("__array"); // internal array object used by vectors
	lengthID = val_id("length");
	getLengthID = val_id("get_length");
	setLengthID = val_id("set_length");
	pushID = val_id("push");
	frameID = val_id("frame");
	uvID = val_id("uv");
	parentID = val_id("parent");
	mapGetID = val_id("get");
	extraID = val_id("extra");
	bodyID = val_id("body");
	capID = val_id("cap");

	holdBodyFramesID = val_id("holdBodyFrames");
	holdCapFramesID = val_id("capBodyFrames");

	visibleID = val_id("visible");
	sourceSizeID = val_id("sourceSize");

	holdGraphicID = val_id("holdGraphic");
	bodyFrameID = val_id("bodyFrame");
	capFrameID = val_id("capFrame");

	identityID = val_id("identity");
	appendScaleID = val_id("appendScale");
	appendRotationID = val_id("appendRotation");
	appendTranslationID = val_id("appendTranslation");
	transformVectorID = val_id("transformVector");
	setToID = val_id("setTo");

	rawDataID = val_id("rawData");
	projectID = val_id("project");

	return alloc_int(1);
}
DEFINE_PRIM(getIDs, 0);

static void setLimeStuff()
{

}

static value cur_camera;
static void setCurCamera(value camera)
{
	cur_camera = camera;
}
DEFINE_PRIM(setCurCamera, 1);

static value cur_tempgraphic;
static void setCurTempGraphic(value graphic)
{
	cur_tempgraphic = graphic;
}
DEFINE_PRIM(setCurTempGraphic, 1);

static value X_AXIS;
static value Y_AXIS;
static value Z_AXIS;
static void setAxes(value x, value y, value z)
{
	X_AXIS = x;
	Y_AXIS = y;
	Z_AXIS = z;
}
DEFINE_PRIM(setAxes, 3);

static value matrix;
static double playFieldX = .0;
static double playFieldY = .0;
static double playFieldZ = .0;
static bool hasPlayFieldTransform = false;

static double __tanHalfFov;
static double __depthRange;
static double __depthScale;
static double __depthOffset;
static void setFov(value inFov)
{
	const double aspect = 1280/720;
	const double fov = val_float(inFov) * TO_RAD;
	const double ffar = 1.0;
	const double fnear = 0.0;

	__tanHalfFov = tan(fov * .5);
	__depthRange = 1 / (fnear - ffar);
	__depthScale = (fnear + ffar) * __depthRange;
	__depthOffset = 2 * fnear * (ffar * __depthRange);
}
DEFINE_PRIM(setFov, 1);

static double holdGrain = 16.0;

static value updatePlayFieldMatrix(value m)
{
	matrix = m;
	lua_getglobal(cur_l, "playFieldTransform");
	lua_pushnumber(cur_l, cur_pn);
	lua_call(cur_l, 1, 12);

	// playfield position
	lua_Number pfx = lua_tonumber(cur_l, -12);
	lua_Number pfy = lua_tonumber(cur_l, -11);
	lua_Number pfz = lua_tonumber(cur_l, -10);
	// playfield rotation
	lua_Number pfrx = lua_tonumber(cur_l, -9);
	lua_Number pfry = lua_tonumber(cur_l, -8);
	lua_Number pfrz = lua_tonumber(cur_l, -7);
	// playfield zoom
	lua_Number pfzw = lua_tonumber(cur_l, -6);
	lua_Number pfzx = lua_tonumber(cur_l, -5);
	lua_Number pfzy = lua_tonumber(cur_l, -4);
	lua_Number pfzz = lua_tonumber(cur_l, -3);
	// playfield skew
	lua_Number pfsx = lua_tonumber(cur_l, -2);
	lua_Number pfsy = lua_tonumber(cur_l, -1);

	hasPlayFieldTransform = pfrx != 0 || pfry != 0 || pfrz != 0 || pfzw != 1 || pfzx != 1 || pfzy != 1 || pfzz != 1 || pfsx != 0 || pfsy != 0;

	playFieldX = pfx;
	playFieldY = pfy;
	playFieldZ = pfz;
	
	lua_pop(cur_l, 12);
	
	val_call0(val_field(matrix, identityID));

	if (hasPlayFieldTransform)
	{
		val_call3(val_field(matrix, appendScaleID), alloc_float(pfzx * pfzw), alloc_float(pfzy * pfzw), alloc_float(pfzz * pfzw));
	
		const value appendRotation = val_field(matrix, appendRotationID);
		val_call3(appendRotation, alloc_float(pfrx), X_AXIS, val_null);
		val_call3(appendRotation, alloc_float(pfry), Y_AXIS, val_null);
		val_call3(appendRotation, alloc_float(pfrz), Z_AXIS, val_null);
	}

	return alloc_bool(hasPlayFieldTransform);
}
DEFINE_PRIM(updatePlayFieldMatrix, 1);

// rendering
static inline void transformVertex(double &vx, double &vy, double &vz, double x,double y,double z, double rx,double ry,double rz, double zx,double zy,double zz, double originX,double originY, double scaleX,double scaleY, double offsetX,double offsetY)
{
	vx -= originX;
	vy -= originY;

	vx *= scaleX * zx;
	vy *= scaleY * zy;

	vx -= offsetX * zx;
	vy -= offsetY * zy;

	rotate3(vx, vy, vz, rx, ry, rz);

	vx += originX;
	vy += originY;

	vx += x;
	vy += y;
	vz += z;

	// this is stupid
	// Matrix3DUtils.transformVector from away3d
	if (hasPlayFieldTransform)
	{
		const double *raw = val_array_double(val_field(val_field(matrix, rawDataID), arrayID));
	
		const double a = raw[0];
		const double e = raw[1];
		const double i = raw[2];
		const double m = raw[3];
		const double b = raw[4];
		const double f = raw[5];
		const double j = raw[6];
		const double n = raw[7];
		const double c = raw[8];
		const double g = raw[9];
		const double k = raw[10];
		const double o = raw[11];
		const double d = raw[12];
		const double h = raw[13];
		const double l = raw[14];
		const double p = raw[15];
	
		const double ox = a * vx + b * vy + c * vz + d;
		const double oy = e * vx + f * vy + g * vz + h;
		const double oz = i * vx + j * vy + k * vz + l;
		//const double ow = m * vx + n * vy + o * vz + p;
	
		vx = ox;
		vy = oy;
		vz = oz;
	}

	if (vz != 0.0)
	{
		vz += playFieldZ;
	
		const double projectedZ = __depthScale * min((vz*0.00078125) - 1, 0) + __depthOffset;
		const double projectedFov = (__tanHalfFov / projectedZ);
		
		vx *= projectedFov;
		vy *= projectedFov;
		vz = SCALE(projectedZ, 2, 0, 0, 1);
	}
	else
	{
		vz = 1.0;
	}

	vx += playFieldX;
	vy += playFieldY;
}
//
#define pushPos(vvx, vvy, vvz, u, v) \
{ \
	double vx = vvx; \
	double vy = vvy; \
	double vz = vvz; \
 	\
	transformVertex(vx,vy,vz, x,y,z, rx,ry,rz, zx,zy,zz, originX,originY, scalex,scaley, offsetX, offsetY); \
	\
	push(uvtData, u); \
	push(uvtData, v); \
	push(uvtData, alloc_float(vz)); \
	\
	push(vertices, alloc_float(vx)); \
	push(vertices, alloc_float(vy)); \
} \

static value *batchArgs = new value[6];
static inline void renderSprite(double setPos, value sprite, double x,double y,double z, double rx,double ry,double rz, double zx,double zy,double zz, double sx,double sy, double scalex, double scaley, double originX, double originY, double offsetX, double offsetY)
{
	const value frame = val_field(sprite, frameID);
	const value frameRect = val_field(frame, frameID);
	const value frameOffset = val_field(frame, offsetID);
	const value graphic = val_field(frame, parentID);

	batchArgs[0] = graphic; // graphic
	batchArgs[1] = val_field(sprite, antialiasingID); // smoothing
	batchArgs[2] = val_false; // colored
	batchArgs[3] = val_null; // blend
	batchArgs[4] = val_false; // hasColorOffsets
	batchArgs[5] = val_null; // shader

	const value drawItem = val_callN(val_field(cur_camera, startTrianglesBatchID), batchArgs, 6);
	
	const value vertices = val_field(drawItem, verticesID);
	const int numVertices = val_int(val_field(val_field(vertices, arrayID), lengthID)) / 2;

	const value indices = val_field(drawItem, indicesID);

	const value uvtData = val_field(drawItem, uvtDataID);

	value colorMultipliers = val_field(drawItem, colorMultipliersID);
	value colorOffsets = val_field(drawItem, colorOffsetsID);

	//if (!colorMultipliers)
	//{
	//	colorMultipliers = alloc_array(0);
	//	colorOffsets = alloc_array(0);

	//	alloc_field(drawItem, colorMultipliersID, colorMultipliers);
	//	alloc_field(drawItem, colorOffsetsID, colorOffsets);
	//}

	const value alphas = val_field(drawItem, alphasID);

	const value uv = val_field(frame, uvID);

	const value uvLeft = val_field(uv, xID);
	const value uvRight = val_field(uv, widthID);
	const value uvTop = val_field(uv, yID);
	const value uvBottom = val_field(uv, heightID);

	const double frameWidth = val_float(val_field(frameRect, widthID));
	const double frameHeight = val_float(val_field(frameRect, heightID));

	double left = val_float(val_field(frameOffset, xID));
	double right = frameWidth + left;

	double top = val_float(val_field(frameOffset, yID));
	double bottom = frameHeight + top;

	double forward = 0.0;
	double back = 0.0;

	//const double frameAngle = val_float(val_field(frame, angleID)) * TO_RAD;
	//if (frameAngle != 0)
	//{
	//	double l1, l2 = left, left;
	//	double r1, r2 = right, right;
	//	double t1, t2 = top, top;
	//	double b1, b2 = bottom, bottom;

	//	rotate(l1, t1, frameAngle);
	//	rotate(r2, t2, frameAngle);

	//}
	//push(uvtData, alloc_float(1.0)); // depth later

	if (setPos)
	{
		double fx, fy, fz = 0;
		transformVertex(fx, fy, fz, x,y,z, rx,ry,rz, zx,zy,zz, .0,.0, scalex,scaley, .0, .0);
		alloc_field(sprite, xID, alloc_float(fx));
		alloc_field(sprite, yID, alloc_float(fy));
	}
	
	pushPos(left, top, z, uvLeft, uvTop);
	pushPos(right, top, z, uvRight, uvTop);
	pushPos(left, bottom, z, uvLeft, uvBottom);
	pushPos(right, bottom, z, uvRight, uvBottom);

	push(indices, alloc_int(numVertices));
	push(indices, alloc_int(numVertices + 1));
	push(indices, alloc_int(numVertices + 2));
	push(indices, alloc_int(numVertices + 1));
	push(indices, alloc_int(numVertices + 2));
	push(indices, alloc_int(numVertices + 3));

	for (int o = 0; o < 6; o++)
	{
		val_array_push(alphas, val_field(cur_camera, alphaID));
		//for (int oo = 0; oo < 4; oo++)
		//{
		//	push(colorMultipliers, alloc_float(1.0));
		//	push(colorOffsets, alloc_float(0.0));
		//}
	}
}

static inline void getYOffset(double distance, int col, double &fYOffset)
{
	lua_getglobal(cur_l, "getYAdjust");
	lua_pushnumber(cur_l, distance);
	lua_pushnumber(cur_l, col);
	lua_pushnumber(cur_l, cur_pn);
	
	lua_call(cur_l, 3, 1);

	fYOffset = lua_tonumber(cur_l, -1);
	lua_pop(cur_l, 1);
}

static inline void getPosition(double fYOffset, int col, double &x,double &y,double &z, double &rx,double &ry,double &rz, double &zx,double &zy,double &zz, double &sx,double &sy)
{
	lua_getglobal(cur_l, "arrowEffects");
	lua_pushnumber(cur_l, fYOffset);
	lua_pushnumber(cur_l, col);
	lua_pushnumber(cur_l, cur_pn);

	lua_call(cur_l, 3, 11);

	// position
	x = lua_tonumber(cur_l, -11);
	y = lua_tonumber(cur_l, -10);
	z = lua_tonumber(cur_l, -9);
	// rotation
	rx = lua_tonumber(cur_l, -8);
	ry = lua_tonumber(cur_l, -7);
	rz = lua_tonumber(cur_l, -6);
	// scale
	zx = lua_tonumber(cur_l, -5);
	zy = lua_tonumber(cur_l, -4);
	zz = lua_tonumber(cur_l, -3);
	// skew
	sx = lua_tonumber(cur_l, -2);
	sy = lua_tonumber(cur_l, -1);
	lua_pop(cur_l, 11);
}

// renders receptors, splashes, and covers
static void renderReceptorAttachment(value sprite, int col)
{
	const double distance = .0;

	const value colorTransform = val_field(sprite, colorTransformID);
	const value scale = val_field(sprite, scaleID);
	const value offset = val_field(sprite, offsetID);
	const value origin = val_field(sprite, originID);

	const double scalex = val_float(val_field(scale, xID));
	const double scaley = val_float(val_field(scale, yID));

	const double offsetX = val_float(val_field(offset, xID));
	const double offsetY = val_float(val_field(offset, yID));

	const double originX = val_float(val_field(origin, xID));
	const double originY = val_float(val_field(origin, yID));

	// position
	double x = .0;
	double y = .0;
	double z = .0;
	// rotation
	double rx = .0;
	double ry = .0;
	double rz = .0;
	// scale
	double zx = .0;
	double zy = .0;
	double zz = .0;
	// skew
	double sx = .0;
	double sy = .0;

	double fYOffset = .0;
	getYOffset(distance, col, fYOffset);
	getPosition(fYOffset, col, x,y,z, rx,ry,rz, zx,zy,zz, sx,sy);

	lua_getglobal(cur_l, "receptorAlpha");
	lua_pushnumber(cur_l, col);
	lua_pushnumber(cur_l, cur_pn);

	lua_call(cur_l, 2, 1);
	//

	const lua_Number alp = lua_tonumber(cur_l, -1);
	lua_pop(cur_l, 1);
	// prepare draw item
	renderSprite(true, sprite, x,y,z, rx,ry,rz, zx,zy,zz, sx,sy, scalex, scaley, originX, originY, offsetX, offsetY);
}

static void renderReceptors(value receptor)
{
	if (!cur_l || !cur_camera || !receptor)
	{
		return;
	}
	value visible = val_field(receptor, visibleID);
	if (visible == val_false)
	{
		return;
	}
	getIDs();

	// prepare positions and scales and rotations
	alloc_field(receptor, updateNotesPosXID, val_false);
	alloc_field(receptor, updateNotesPosYID, val_false);

	const int col = val_int(val_field(receptor, idID));

	renderReceptorAttachment(receptor, col);
}
DEFINE_PRIM(renderReceptors, 1);

#define distanceUvt(dist, bodyHeight, bodyUVBottom, bodyUVTop) SCALE(dist, 0, -bodyHeight, bodyUVBottom, bodyUVTop);


static void renderHolds(value sprite)
{
	if (!cur_l || !cur_camera || !sprite || val_bool(val_field(sprite, isSustainNoteID)))
		return;
	const double sustainLength = val_float(val_field(sprite, sustainLengthID));
	if (sustainLength <= .0)
		return;

	const bool wasGoodHit = val_field(sprite, wasGoodHitID) == val_true;

	const int col = val_int(val_field(sprite, noteDataID));
	const double strumTime = val_float(val_field(sprite, strumTimeID));

	const value extra = val_field(sprite, extraID);
	const value getExtra = val_field(extra, val_id("get"));

	const value graphic = val_call1(getExtra, alloc_string("holdGraphic"));

	const value bodyFrame = val_call1(getExtra, alloc_string("bodyFrame"));
	const value bodySourceSize = val_field(bodyFrame, sourceSizeID);
	const value bodyFrameUV = val_field(bodyFrame, uvID);

	const double bodyUVLeft = val_float(val_field(bodyFrameUV, xID));
	const double bodyUVTop = val_float(val_field(bodyFrameUV, yID));
	const double bodyUVRight = val_float(val_field(bodyFrameUV, widthID));
	const double bodyUVBottom = val_float(val_field(bodyFrameUV, heightID));
	const double bodyHalfU = lerp(bodyUVLeft, bodyUVRight, 0.5);
	
	const value capFrame = val_call1(getExtra, alloc_string("capFrame"));
	const value capSourceSize = val_field(capFrame, sourceSizeID);
	const value capFrameUV = val_field(capFrame, uvID);

	const double capUVLeft = val_float(val_field(capFrameUV, xID));
	const double capUVTop = val_float(val_field(capFrameUV, yID));
	const double capUVRight = val_float(val_field(capFrameUV, widthID));
	const double capUVBottom = val_float(val_field(capFrameUV, heightID));
	const double capHalfU = lerp(capUVLeft, capUVRight, 0.5);

	const value scale = val_field(sprite, scaleID);
	const value drag = val_field(sprite, dragID);
	const value offset = val_field(sprite, offsetID);
	const value origin = val_field(sprite, originID);

	const double offsetX = val_float(val_field(offset, xID));
	const double offsetY = val_float(val_field(offset, yID));

	const double originX = val_float(val_field(origin, xID));
	const double originY = val_float(val_field(origin, yID));
	
	const double scalex = val_float(val_field(scale, xID));
	const double scaley = val_float(val_field(scale, yID));

	const double bodyWidth = val_float(val_field(bodySourceSize, xID));
	const double bodyHeight = val_float(val_field(bodySourceSize, yID));
	const double capWidth = val_float(val_field(capSourceSize, xID));
	const double capHeight = val_float(val_field(capSourceSize, yID));

	//const double bodyWidth = 

	batchArgs[0] = graphic; // graphic
	batchArgs[1] = val_field(sprite, antialiasingID); // smoothing
	batchArgs[2] = val_false; // colored
	batchArgs[3] = val_null; // blend
	batchArgs[4] = val_false; // hasColorOffsets
	batchArgs[5] = val_null; // shader

	const value drawItem = val_callN(val_field(cur_camera, startTrianglesBatchID), batchArgs, 6);
	
	const value vertices = val_field(drawItem, verticesID);
	const int numVertices = val_int(val_field(val_field(vertices, arrayID), lengthID)) / 2;

	const value indices = val_field(drawItem, indicesID);

	const value uvtData = val_field(drawItem, uvtDataID);

	value colorMultipliers = val_field(drawItem, colorMultipliersID);
	value colorOffsets = val_field(drawItem, colorOffsetsID);
	
	const value alphas = val_field(drawItem, alphasID);

	// position
	double x = .0;
	double y = .0;
	double z = .0;
	// rotation
	double rx = .0;
	double ry = .0;
	double rz = .0;
	// scale
	double zx = .0;
	double zy = .0;
	double zz = .0;
	// skew
	double sx = .0;
	double sy = .0;

	const double distance1 = wasGoodHit ? .0 : 0.45 * (strumTime - songPosition);
	const double distance2 = 0.45 * ((strumTime + sustainLength) - songPosition);
	
	const double cutoff = arrowSize * .5;

	double fYOffset1 = .0;
	getYOffset(distance1, col, fYOffset1);
	fYOffset1 += cutoff;
	double fYOffset2 = .0;
	getYOffset(distance2, col, fYOffset2);

	if (fYOffset2 <= cutoff && wasGoodHit)
	{
		return;
	}

	double length = fYOffset2 - fYOffset1;
	double bodyLength = length - capHeight;
	const double totalV = distanceUvt(bodyLength, bodyHeight * val_float(val_field(val_field(bodyFrame, offsetID), yID)), bodyUVBottom, bodyUVTop);
	
	const double endRatio = bodyLength / length;
	const double ratio = bodyLength / holdGrain;
	double capYOffset = lerp(fYOffset1, fYOffset2, endRatio);

	double clippedCapUVTop = capUVTop;
	if (wasGoodHit && capYOffset < cutoff)
	{
		clippedCapUVTop = lerp(capUVTop, capUVBottom, min(1, (cutoff - capYOffset) / capHeight));
		capYOffset = cutoff;
	}


	int i = 0;
	int i3 = 0;
	int i6 = 0;
	int i9 = 0;
	int i12 = 0;

	bool tooFar = false;

	double outx, outy = 0;
	
	getPosition(fYOffset1, col, x,y,z, rx,ry,rz, zx,zy,zz, sx,sy);
	pushPos((arrowSize/scalex)*.5-bodyWidth*.5, .0, .0, alloc_float(bodyUVLeft), alloc_float(-totalV));
	pushPos((arrowSize/scalex)*.5+bodyWidth*.5, .0, .0, alloc_float(bodyUVRight), alloc_float(-totalV));

	getPosition(capYOffset, col, x,y,z, rx,ry,rz, zx,zy,zz, sx,sy);
	pushPos((arrowSize/scalex)*.5-bodyWidth*.5, .0, .0, alloc_float(bodyUVLeft), alloc_float(bodyUVBottom));
	pushPos((arrowSize/scalex)*.5+bodyWidth*.5, .0, .0, alloc_float(bodyUVRight), alloc_float(bodyUVBottom));
	pushPos((arrowSize/scalex)*.5-capWidth*.5, .0, .0, alloc_float(capUVLeft), alloc_float(clippedCapUVTop));
	pushPos((arrowSize/scalex)*.5+capWidth*.5, .0, .0, alloc_float(capUVRight), alloc_float(clippedCapUVTop));


	getPosition(fYOffset2, col, x,y,z, rx,ry,rz, zx,zy,zz, sx,sy);
	pushPos((arrowSize/scalex)*.5-capWidth*.5, .0, .0, alloc_float(capUVLeft), alloc_float(capUVBottom));
	pushPos((arrowSize/scalex)*.5+capWidth*.5, .0, .0, alloc_float(capUVRight), alloc_float(capUVBottom));

	push(indices, alloc_int(numVertices));
	push(indices, alloc_int(numVertices + 1));
	push(indices, alloc_int(numVertices + 2));
	push(indices, alloc_int(numVertices + 1));
	push(indices, alloc_int(numVertices + 2));
	push(indices, alloc_int(numVertices + 3));

	push(indices, alloc_int(numVertices + 4));
	push(indices, alloc_int(numVertices + 4 + 1));
	push(indices, alloc_int(numVertices + 4 + 2));
	push(indices, alloc_int(numVertices + 4 + 1));
	push(indices, alloc_int(numVertices + 4 + 2));
	push(indices, alloc_int(numVertices + 4 + 3));

	for (int o = 0; o < 12; o++)
	{
		val_array_push(alphas, val_field(cur_camera, alphaID));
		//for (int oo = 0; oo < 4; oo++)
		//{
		//	push(colorMultipliers, alloc_float(1.0));
		//	push(colorOffsets, alloc_float(0.0));
		//}
	}
	//double left = v
}
DEFINE_PRIM(renderHolds, 1);

static void renderArrows(value note)
{
	if (!cur_l || !cur_camera || !note || val_bool(val_field(note, isSustainNoteID)))
		return;
	value visible = val_field(note, visibleID);
	if (visible == val_false)
	{
		return;
	}

	alloc_field(note, strumRelativePosID, val_false);
	alloc_field(note, updateNotesPosXID, val_false);
	alloc_field(note, updateNotesPosYID, val_false);
	alloc_field(note, copyStrumAngleID, val_false);

	const double strumTime = val_float(val_field(note, strumTimeID));
	const int col = val_int(val_field(note, noteDataID));

	const double distance = 0.45 * (strumTime - songPosition);

	const value colorTransform = val_field(note, colorTransformID);
	const value scale = val_field(note, scaleID);
	const value offset = val_field(note, offsetID);
	const value origin = val_field(note, originID);

	const double offsetX = val_float(val_field(offset, xID));
	const double offsetY = val_float(val_field(offset, yID));

	const double originX = val_float(val_field(origin, xID));
	const double originY = val_float(val_field(origin, yID));

	// position
	double x = .0;
	double y = .0;
	double z = .0;
	// rotation
	double rx = .0;
	double ry = .0;
	double rz = .0;
	// scale
	double zx = .0;
	double zy = .0;
	double zz = .0;
	// skew
	double sx = .0;
	double sy = .0;

	double fYOffset = .0;
	getYOffset(distance, col, fYOffset);
	getPosition(fYOffset, col, x,y,z, rx,ry,rz, zx,zy,zz, sx,sy);

	lua_getglobal(cur_l, "arrowAlphaGlow");
	lua_pushnumber(cur_l, distance);
	lua_pushnumber(cur_l, col);
	lua_pushnumber(cur_l, cur_pn);

	lua_call(cur_l, 3, 2);

	const lua_Number alp = lua_tonumber(cur_l, -2);
	const lua_Number glow = lua_tonumber(cur_l, -1);
	lua_pop(cur_l, 2);

	const double scalex = val_float(val_field(scale, xID));
	const double scaley = val_float(val_field(scale, yID));

	const double colorOffset = glow * 255.0;
	
	renderSprite(false, note, x,y,z, rx,ry,rz, zx,zy,zz, sx,sy, scalex, scaley, originX, originY, offsetX, offsetY);
}
DEFINE_PRIM(renderArrows, 1);

static void renderHoldCovers(value holdCover)
{
	if (!cur_l || !cur_camera || !holdCover)
	{
		return;
	}
	value visible = val_field(holdCover, visibleID);

	if (visible == val_false)
	{
		return;
	}

	const int col = val_int(val_field(holdCover, idID));

	renderReceptorAttachment(holdCover, col);
}
DEFINE_PRIM(renderHoldCovers, 1);

static void renderHoldSplashes(value splash)
{

}
DEFINE_PRIM(renderHoldSplashes, 1);

static void renderNoteSplashes(value splash)
{

}
DEFINE_PRIM(renderNoteSplashes, 1);

extern "C" void lua_main()
{
	// no neko no neko
	//kind_share(&kind_lua_vm, "lua::vm"); // Fix Neko init
}
DEFINE_ENTRY_POINT(lua_main);

extern "C" int lua_register_prims() { return 0; }
