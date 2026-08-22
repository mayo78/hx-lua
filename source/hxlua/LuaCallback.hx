package hxlua;

import haxe.Constraints.Function;

/**
 * An object that can be treated like a function, but also must be disposed of when done with it :)
 * IMplments IFlxDestroyable if flixel is here
 */
@:forward(destroy)
abstract LuaCallback(_LuaCallback) from _LuaCallback
{
	public function new(lua:Lua, ref:Int)
	{
		this = new _LuaCallback(lua, ref);
		this.__func = Reflect.makeVarArgs(call);
	}

	public function call(args:Array<Any> = null):Null<Any> 
	{
		final l = this.lua.handle;
		Lua.lua_rawgeti(l, Lua.LUA_REGISTRYINDEX, this.ref);
		if (Lua.lua_isfunction(l, -1)) 
		{
			if (args != null)
			{
				for (arg in args)
				{
					if (!this.lua.toLua(arg))
						this.lua.toLua(null);
				}
			}
			var status:Int = Lua.lua_pcall(l, args?.length ?? 0, 1, 0);
			if (status != Lua.LUA_OK)
			{
				var err:String = Lua.lua_tostring(l, -1);
				Lua.lua_pop(l, 1);
				//if (err != null) err = err.trim();
				if (err == null || err.length <= 0)
				{
					switch(status)
					{
						case Lua.LUA_ERRRUN: err = "Runtime Error";
						case Lua.LUA_ERRMEM: err = "Memory Allocation Error";
						case Lua.LUA_ERRERR: err = "Critical Error";
						default: err = "Unknown Error";
					}
				}
				trace("Error on callback: " + err);
				return null;
			}
			else 
			{
				final out = this.lua.fromLua(-1);
				Lua.lua_pop(l, 1);
				return out;
			}
		}
		return null;
	}

	@:to
	public function toFunction():Function
	{
		return this.__func;
	}

	@:op(a()) private function callOp(...args:Any):Any
	{
		return call(args.toArray());
	}
}

private final class _LuaCallback #if flixel implements flixel.util.FlxDestroyUtil.IFlxDestroyable #end
{
	public final lua:Lua;
	public final ref:Int;

	public var __func:Function;

	public function new(lua:Lua, ref:Int)
	{
		this.lua = lua;
		this.ref = ref;
	}

	public function destroy()
	{
		Lua.luaL_unref(lua.handle, Lua.LUA_REGISTRYINDEX, ref);
	}
}