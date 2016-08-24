#ifndef _INCLUDE_LCONF_
#define _INCLUDE_LCONF_

#include <string>
#include <lua.hpp>

using namespace std;

class luaConf
{
public:

	luaConf();
	~luaConf();
	virtual const char *name();
	virtual bool loadConf(const char *conf);
	bool getString(string varName, string &varValue);
	bool getBool(string varName);
	bool getInt(string varName, int &i);
	bool getDouble(string varName, double &d);
	bool getFloat(string varName, float &f);
protected:
	lua_State *L;
private:
};

#endif
