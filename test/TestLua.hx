class TestLua extends haxe.unit.TestCase
{

	public function testVersion()
	{
		assertEquals("Lua 5.2", Lua.version);
	}

	public function testNull()
	{
		assertEquals(null, Lua.run("return null"));
	}

	public function testBoolean()
	{
		assertTrue(Lua.run("return true"));
		assertFalse(Lua.run("return false"));
	}

	public function testArray()
	{
		assertEquals(1, Lua.run("if arr[2] then return 1 else return 2 end", {
			arr: [false, true, false]
		}));
	}

	public function testInteger()
	{
		assertEquals(15, Lua.run("return num", {num: 15}));
	}

	public function testFloat()
	{
		assertEquals(15.3, Lua.run("return num", {num: 15.3}));
	}

	public function testObjectToTable()
	{
		assertTrue(Lua.run('return foo.bar', {foo: {bar: true}}));
	}

	public function testReturnTable()
	{
		var result:Array<Dynamic> = Lua.run('return {"foo", true, 8.3, 6}');
		assertEquals(4, result.length);
		assertEquals("foo", result[0]);
		assertEquals(true, result[1]);
		assertEquals(8.3, result[2]);
		assertEquals(6, result[3]);
	}

	public function testReturnObject()
	{
		var result = Lua.run('return {foo = true, bar = 95}');
		assertEquals(true, result.foo);
		assertEquals(95, result.bar);
	}

	public function testEmbededObjects()
	{
		var result = Lua.run('return {foo = {bar = {baz = 30}, foo = 99}, bar = {14, 52, 25, 1, 7}}');
		assertEquals(30, result.foo.bar.baz);
		assertEquals(99, result.foo.foo);
		assertEquals(5, cast(result.bar, Array<Dynamic>).length);
		assertEquals(25, result.bar[2]);
	}

	public function testFunctionNoArgs()
	{
		assertEquals(true, Lua.run("return num()", {num: function() { return true; }}));
	}

	public function testFunctionArgs()
	{
		assertEquals(15, Lua.run("return num(true, 1)", {
			num: function(a:Bool, b:Int) { return 15; }
		}));
	}

	public function testFunctionPassThrough()
	{
		assertEquals("hello world", Lua.run('return greet(message)', {
			message: "hello world",
			greet: function(greeting:String) { return greeting; }
		}));
	}

	public function testMultipleInstances()
	{
		var l1 = new Lua(),
			l2 = new Lua();

		var context = {foo: 1};
		l1.setVars(context);

		assertEquals(1, l1.execute("return foo"));

		// change the context for l2
		context.foo = 2;
		l2.setVars(context);

		assertEquals(1, l1.execute("return foo"));
		assertEquals(2, l2.execute("return foo"));

		// change foo on l1 but not l2
		context.foo = 3;
		l1.setVars(context);

		assertEquals(3, l1.execute("return foo"));
		assertEquals(2, l2.execute("return foo"));
	}

	public function testCallLuaFunction()
	{
		var lua = new Lua();
		lua.execute("-- comment line
function add(a, b)
	return a + b
end

function sub(a, b)
	return a - b
end");

		assertEquals(8, lua.call("add", [2, 6]));
		assertEquals(29, lua.call("sub", [36, 7]));

		assertEquals(null, lua.call("fail", 3)); // fails due to missing function
		assertEquals(null, lua.call("sub", { fail: 3 })); // fails due to wrong number of arguments
	}

	public static function testLuaThings()
	{
		var lua = new Lua();
		
		// easy and safe
		Lua.lua_pushboolean(lua.handle, true);
		Lua.lua_setglobal(lua.handle, "boolean");
		assertEquals(true, lua.execute("return boolean"));

		Lua.lua_pushnumber(lua.handle, 1.5);
		Lua.lua_setglobal(lua.handle, "number");
		assertEquals(1.5, lua.execute("return boolean"));

		Lua.lua_pushstring(lua.handle, "a string");
		Lua.lua_setglobal(lua.handle, "str");
		assertEquals("a string", lua.execute("return str"));

		// scary
		Lua.haxe_to_lua(lua.handle, [12, 13, 14]);
		Lua.lua_setglobal(lua.handle, "arr");
		assertEquals(13, lua.execute("return arr[2]"));

		Lua.haxe_to_lua(lua.handle, {value: "yes"});
		Lua.lua_setglobal(lua.handle, "t");
		assertEquals("yes", lua.execute("return t.value"));
		
		Lua.haxe_to_lua(lua.handle, function() return 1);
		Lua.lua_setglobal(lua.handle, "func");
		assertEquals(1, lua.execute("return func()"));
		
		Lua.haxe_to_lua(lua.handle, function(inp:String) return 'inp: $inp');
		Lua.lua_setglobal(lua.handle, "funcargs");
		assertEquals('inp: good', lua.execute("return funcargs 'good'"));
	}

	public static function main()
	{
		var runner = new haxe.unit.TestRunner();
		runner.add(new TestLua());
		runner.run();
	}

}
