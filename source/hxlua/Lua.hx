package hxlua;

import haxe.DynamicAccess;
#if cpp
import cpp.Lib;
#end

using StringTools;

class Lua
{
	// lua constants
	


	/* option for multiple returns in `lua_pcall' and `lua_call' */
	public static inline var LUA_MULTRET:Int = (-1);


	/* pseudo-indices */

	public static inline var LUA_REGISTRYINDEX:Int = (-10000);
	public static inline var LUA_ENVIRONINDEX:Int  = (-10001);
	public static inline var LUA_GLOBALSINDEX:Int  = (-10002);


	/* thread status */

	public static inline var LUA_OK:Int          = 0;
	public static inline var LUA_YIELD:Int       = 1;
	public static inline var LUA_ERRRUN:Int      = 2;
	public static inline var LUA_ERRSYNTAX:Int   = 3;
	public static inline var LUA_ERRMEM:Int      = 4;
	public static inline var LUA_ERRERR:Int      = 5;


	/* basic types */

	public static inline var LUA_TNONE:Int           = (-1);

	public static inline var LUA_TNIL:Int            = 0;
	public static inline var LUA_TBOOLEAN:Int        = 1;
	public static inline var LUA_TLIGHTUSERDATA:Int  = 2;
	public static inline var LUA_TNUMBER:Int         = 3;
	public static inline var LUA_TSTRING:Int         = 4;
	public static inline var LUA_TTABLE:Int          = 5;
	public static inline var LUA_TFUNCTION:Int       = 6;
	public static inline var LUA_TUSERDATA:Int       = 7;
	public static inline var LUA_TTHREAD:Int         = 8;


	/* minimum Lua stack available to a C function */

	public static inline var LUA_MINSTACK:Int        = 20;


	/* garbage-collection function and options */

	public static inline var LUA_GCSTOP:Int         = 0;
	public static inline var LUA_GCRESTART:Int      = 1;
	public static inline var LUA_GCCOLLECT:Int      = 2;
	public static inline var LUA_GCCOUNT:Int        = 3;
	public static inline var LUA_GCCOUNTB:Int       = 4;
	public static inline var LUA_GCSTEP:Int         = 5;
	public static inline var LUA_GCSETPAUSE:Int     = 6;
	public static inline var LUA_GCSETSTEPMUL:Int   = 7;

		/*
	** {======================================================================
	** Debug API
	** =======================================================================
	*/

	/* Event codes */

	public static inline var LUA_HOOKCALL:Int      = 0;
	public static inline var LUA_HOOKRET:Int       = 1;
	public static inline var LUA_HOOKLINE:Int      = 2;
	public static inline var LUA_HOOKCOUNT:Int     = 3;
	public static inline var LUA_HOOKTAILRET:Int  = 4;


	/* Event masks */

	public static inline var LUA_MASKCALL:Int  = (1 << LUA_HOOKCALL);
	public static inline var LUA_MASKRET:Int   = (1 << LUA_HOOKRET);
	public static inline var LUA_MASKLINE:Int  = (1 << LUA_HOOKLINE);
	public static inline var LUA_MASKCOUNT:Int = (1 << LUA_HOOKCOUNT);

	
    /* compatibility with ref system */

    /* predefined references */
    public static inline var LUA_NOREF:Int   = (-2);
    public static inline var LUA_REFNIL:Int  = (-1);

	/**
	 * Creates a new lua vm state
	 */
	public function new()
	{
		handle = lua_create();
		lua_openlibs(handle);
	}

	/**
	 * Get the version string from Lua
	 */
	public static var version(get, never):String;
	private inline static function get_version():String
	{
		return lua_get_version();
	}

	/**
	 * Defines variables in the lua vars
	 * @param vars An object defining the lua variables to create
	 */
	public function setVars(vars:Dynamic):Void
	{
		for (field in Reflect.fields(vars))
			set(field, Reflect.field(vars, field));
	}

	/**
	 * Runs a lua script
	 * @param script The lua script to run in a string
	 * @return The result from the lua script in Haxe
	 */
	public function execute(script:String):Dynamic
	{
		final result = lua_dostring(handle, script);

		if (result != LUA_OK)
		{
			final resultStr = lua_tostring(handle, result);
			throw 'Error on lua script! ' + resultStr; // classic
		}

		var ret = null;
		while (lua_gettop(handle) != 0)
		{
			ret = fromLua(lua_gettop(handle));
			lua_pop(handle, 1);
		}

		return ret;
	}

	/**
	 * Calls a previously loaded lua function
	 * @param func The lua function name (globals only)
	 * @param input A single argument or array of arguments
	 * @return If the lua function returns multiple values, an array will be returned
	 */
	public function call(func:String, input:Dynamic):Dynamic
	{
		lua_getglobal(handle, func);

		final type = lua_type(handle, -1);
		if (type != LUA_TFUNCTION)
		{
			lua_pop(handle, 1);
			throw 'Attempt to call a ${typeToString(type)} ($type) value';
		}

		final args:Array<Dynamic> = switch Type.typeof(input)
		{
			case TClass(Array):
				cast input;
			default:
				[input];
		}

		for (arg in args)
		{
			if (!toLua(arg))
			{
				trace('[ERROR] Cannot convert value ($arg) to lua!');
				toLua(null);
			}
		}

		// TODO: figure out multiret cause its weird counting the thing with gettop it dont work :(
		final status = lua_pcall(handle, args.length, 1, 0);

		//trace("nresults?", nresults);

		if (status == LUA_OK)
		{
			//if (nresults < -1)
			//{
			//	final arr = [];
				
			//	while (nresults < 0)
			//	{
			//		trace("yes", nresults);
			//		arr.push(fromLua(nresults));
			//		lua_pop(handle, 1);
			//		nresults++;
			//	}
	
			//	return arr;
			//}
			//else if (nresults == -1) 
			//{
				//trace("what ok");
				final v = fromLua(-1);
				lua_pop(handle, 1);
				return v;
			//}
		}
		else
		{
			throw getErrorMessage(status);
		}

		return null;
	}

	/**
	 * Convienient way to run a lua script in Haxe without loading any libraries
	 * @param script The lua script to run in a string
	 * @param vars An object defining the lua variables to create
	 * @return The result from the lua script in Haxe
	 */
	public static function run(script:String, ?vars:Dynamic):Dynamic
	{
		var lua = new Lua();
		lua.setVars(vars);
		return lua.execute(script);
	}

	private static function load(func:String, numArgs:Int):Dynamic
	{
		#if cpp
		final f = Lib.load("hxlua", func, numArgs);
		if (f == null)
			throw "Primitive " + func + " not found";
		return f;
		#else
		return null;
		#end
	}

	public function toLua(value:Dynamic):Bool
	{
		switch Type.typeof(value)
		{
			case TNull, TInt, TFloat, TBool, TFunction, TClass(String):
				return haxe_to_lua(handle, value);
			case TClass(Array):
				arrayToLua(value);
				return true;
			case TObject:
				objectToLua(value);
				return true;
			case t:
				trace('[ERROR] Haxe value ($t) not supported');
				return false;
		}
	}

	public function set(name:String, value:Dynamic)
	{
		toLua(value);
		lua_setglobal(handle, name);
	}

	public function fromLua(idx:Int):Any
	{
		final l = handle;
		final vtype = lua_type(l, idx);
		switch vtype
		{
			case LUA_TNIL, LUA_TBOOLEAN, LUA_TNUMBER, LUA_TSTRING:
				return lua_to_haxe(handle, idx);
			case LUA_TTABLE:
				return toHaxeObj(idx);
			case LUA_TFUNCTION:
				var ref = luaL_ref(l, LUA_REGISTRYINDEX);
				return new LuaCallback(this, ref);
			default:
				trace('[ERROR] Lua value (${typeToString(vtype)}) not supported');
				return null;
		}
	}

	function arrayToLua(arr:Array<Any>)
	{
		final l = handle;
		var size:Int = arr.length;
		lua_createtable(l, size, 0);

		for (i in 0...size) 
		{
			toLua(i + 1); // pushes int
			toLua(arr[i]); // pushes any
			lua_settable(l, -3);
		}
	}
	

	function objectToLua(res:Any) 
	{
		final l = handle;
		var tLen = 0;

		for(n in Reflect.fields(res))
			tLen++;

		lua_createtable(l, tLen, 0);
		for (n in Reflect.fields(res))
		{
			toLua(n); // push key
			toLua(Reflect.field(res, n)); // push value
			lua_settable(l, -3);
		}

	}

	function toHaxeObj(i:Int):Any 
	{
		final l = handle;
		var count = 0;
		var array = true;
		// linc_luajit used a macro, which we cant do in hscript, (i mean this version is a non hscript version but the whole point was to hjave it be in hscript eventually)

		function loopTable(v, f)
		{
			toLua(null);
			while(lua_next(l, v < 0 ? v - 1 : v)) 
			{
				f();
				lua_pop(l, 1);
			}
		}

		loopTable(i, function () 
			{
				if(array) {
					if(lua_type(l, -2) != LUA_TNUMBER)
					{
						array = false;
					}
					else 
					{
						var index = lua_tonumber(l, -2);
						if(index < 0 || Std.int(index) != index) 
							array = false;
					}
				}
				count++;
			}
		);

		return if (count == 0) 
		{
			{};
		} 
		else if (array) 
		{
			var v = [];
			loopTable(i, function () 
				{
					var index = Std.int(lua_tonumber(l, -2)) - 1;
					v[index] = fromLua(-1);
				}
			);
			cast v;
		} 
		else 
		{
			var v:DynamicAccess<Any> = {};
			loopTable(i, function () 
				{
					switch lua_type(l, -2) 
					{
						case t if(t == LUA_TSTRING): 
							v.set(lua_tostring(l, -2), fromLua(-1));
						case t if(t == LUA_TNUMBER):
							v.set(Std.string(lua_tonumber(l, -2)), fromLua(-1));
					}
				}
			);
			cast v;
		}
	}

	// some evil people from evil FunkinLua

	function typeToString(type:Int):String
	{
		switch (type)
		{
			case LUA_TBOOLEAN:
				return "boolean";
			case LUA_TLIGHTUSERDATA:
				return "lightuserdata";
			case LUA_TNUMBER:
				return "number";
			case LUA_TSTRING:
				return "string";
			case LUA_TTABLE:
				return "table";
			case LUA_TFUNCTION:
				return "function";
			case LUA_TUSERDATA:
				return "userdata";
			case LUA_TTHREAD:
				return "thread";
		}
		if (type <= LUA_TNIL)
			return "nil";
		return "unknown";
	}

	function getErrorMessage(status:Int):String
	{
		var v:String = lua_tostring(handle, -1);
		lua_pop(handle, 1);

		if (v != null)
			v = v.trim();
		if (v == null || v == "")
		{
			switch (status)
			{
				case LUA_ERRRUN:
					return "Runtime Error";
				case LUA_ERRMEM:
					return "Memory Allocation Error";
				case LUA_ERRERR:
					return "Critical Error";
			}
			return "Unknown Error";
		}

		return v;
	}

	public var handle:Dynamic;

	private static final lua_create:()->Dynamic = load("lua_create", 0);
	private static final lua_get_version:()->String = load("lua_get_version", 0);
	private static final lua_call_function:(handle:Dynamic, func:String, args:Dynamic)->Dynamic = load("lua_call_function", 3);
	private static final lua_load_context:(handle:Dynamic, vars:Dynamic)->Void = load("lua_load_context", 2);
	private static var moduleInit:Bool = false;

	
	private static final haxe_to_lua:(handle:Dynamic, value:Dynamic)->Bool = load("luahx_haxe_to_lua", 2);
	private static final lua_to_haxe:(handle:Dynamic, idx:Int)->Bool = load("luahx_lua_to_haxe", 2);


	// raw lua funtions
	public static final lua_gettop = load("luahx_gettop", 1);
	public static final lua_openlibs = load("luahx_openlibs", 1);
	public static final lua_call = load("luahx_call", 3);
	public static final lua_pcall = load("luahx_pcall", 4);
	public static final lua_dofile = load("luahx_dofile", 2);
	public static final lua_dostring = load("luahx_dostring", 2);
	public static final lua_setglobal = load("luahx_setglobal", 2);
	public static final lua_getglobal = load("luahx_getglobal", 2);
	public static final lua_pop = load("luahx_pop", 2);
	public static final lua_isnumber = load("luahx_isnumber", 2);
	public static final lua_isstring = load("luahx_isstring", 2);
	public static final lua_iscfunction = load("luahx_iscfunction", 2);
	public static final lua_isuserdata = load("luahx_isuserdata", 2);
	public static final lua_isfunction = load("luahx_isfunction", 2);
	public static final lua_istable = load("luahx_istable", 2);
	public static final lua_islightuserdata = load("luahx_islightuserdata", 2);
	public static final lua_isnil = load("luahx_isnil", 2);
	public static final lua_isboolean = load("luahx_isboolean", 2);
	public static final lua_isthread = load("luahx_isthread", 2);
	public static final lua_isnone = load("luahx_isnone", 2);
	public static final lua_isnoneornil = load("luahx_isnoneornil", 2);
	public static final lua_type:(l:Dynamic, idx:Int)->Int = load("luahx_type", 2);
	public static final lua_typename = load("luahx_typename", 2);
	public static final lua_tonumber = load("luahx_tonumber", 2);
	public static final lua_tointeger = load("luahx_tointeger", 2);
	public static final lua_toboolean = load("luahx_toboolean", 2);
	public static final lua_tostring = load("luahx_tostring", 2);
	public static final lua_createtable = load("luahx_createtable", 3);
	public static final lua_settable = load("luahx_settable", 2);
	public static final lua_next = load("luahx_next", 2);
	public static final lua_ref = load("luahx_ref", 2);
	public static final lua_rawgeti = load("luahx_rawgeti", 3);

	public static final luaL_ref = load("luahx_ref", 2);
	public static final luaL_unref = load("luahx_unref", 3);
}
